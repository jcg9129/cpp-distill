// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/pipeline/pipeline.h"
#include "core/report/report.h"
#include "core/render/render.h"
#include "core/render/shorten_names.h"
#include "core/validate/preprocess.h"
#include "core/path.h"
#include "core/temporary.h"
#include <algorithm>
#include <atomic>
#include <fstream>
#include <future>
#include <memory>

namespace distill {
namespace {
template<class T> T take(Result<T> r) { if (!r.ok()) throw r.error(); return std::move(r.value()); }
template<class F> Result<ProcessResult> processTask(F&& task) {
  try { return task(); }
  catch (const Error& e) { return e; }
  catch (const std::exception& e) { return Error{e.what()}; }
  catch (...) { return Error{"process runner threw an unknown exception"}; }
}
using Clock = std::chrono::steady_clock;
bool runnable(const Configuration& c) {
  const auto& t = c.probe.triple;
#if defined(_WIN32)
  const bool os = t.find("mingw") != t.npos || t.find("windows") != t.npos;
#elif defined(__APPLE__)
  const bool os = t.find("darwin") != t.npos || t.find("apple") != t.npos;
#elif defined(__linux__)
  const bool os = t.find("linux") != t.npos;
#else
  const bool os = false;
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
  return os && (t.starts_with("aarch64-") || t.starts_with("arm64-"));
#elif defined(__x86_64__) || defined(_M_X64)
  return os && t.starts_with("x86_64-");
#else
  return false;
#endif
}
bool independentCompilerCommand(const CompileCommand& command) {
  // Unknown flags can name shared side-effect files or load compiler plugins.
  return std::none_of(command.args.begin(),command.args.end(),[](const auto& a) { return a.classification == Class::unmodelled; });
}
void checkedBytes(const std::filesystem::path& p, std::string_view expected) {
  std::ifstream f(p, std::ios::binary);
  const std::string actual{std::istreambuf_iterator<char>(f), {}};
  if (!f || actual != expected) throw Error{"checked work bytes changed: " + pathToUtf8(p)};
}
void write(const std::filesystem::path& p, std::string_view bytes) {
  std::ofstream f(p, std::ios::binary); f << bytes; f.close();
  if (!f) throw Error{"cannot write work file: " + pathToUtf8(p)};
}
struct Pipeline {
  struct ProcessAttempt {
    Result<ProcessResult> outcome = Error{"process not started"};
    std::optional<ProcessRecord> record;
  };
  CompileCommand command;
  const DistillOptions& options;
  ConditionEvaluator& evaluator;
  ProcessRunner runner;
  PipelineResult result;
  TemporaryDirectory work;
  Clock::time_point deadline;
  std::string currentStep;
  std::optional<Configuration> flatConfiguration;
  std::map<std::string,std::vector<ProcessResult>> runs;
  std::map<std::filesystem::path,ProcessAttempt> preprocessed;
  std::future<Result<ProcessResult>> baselineBuild;
  std::optional<PipelineResult> beforeBaseline;
  std::optional<Configuration> baselineConfiguration;
  ProcessRecord baselineRecord;
  std::vector<std::string> speculativeProgress;
  unsigned availableJobs() const { return options.jobs - (baselineBuild.valid() ? 1u : 0u); }
  Pipeline(const CompileCommand& c, const DistillOptions& o, ConditionEvaluator& e, ProcessRunner r)
      : command(c), options(o), evaluator(e), runner(std::move(r)), work(".distill-work-", o.temporaryRoot) {}
  void progress(std::string_view message) {
    if (!options.progress) return;
    if (beforeBaseline) speculativeProgress.emplace_back(message);
    else options.progress(message);
  }
  void step(std::string_view name) {
    deadline = Clock::now() + options.stepTimeout;
    currentStep = name;
    progress(name);
  }
  Result<ProcessResult> limited(const ProcessSpec& input, bool compilation = true, bool preprocessing = false,
                                bool record = true, std::optional<ProcessRecord>* launched = nullptr) {
    auto spec = input;
    // Both the per-run cap and the enclosing phase deadline apply (§3/§4.6).
    const auto processDeadline = compilation ? deadline : std::min(deadline,Clock::now() + options.stepTimeout);
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(processDeadline - Clock::now());
    if (left.count() <= 0) return Error{currentStep + ": step deadline exceeded in " + pathToUtf8(spec.cwd)};
    spec.timeout = std::min(spec.timeout, left);
    spec.maxOutputBytes = std::min(spec.maxOutputBytes, preprocessing ? options.maxPreprocessOutput : options.maxProcessOutput);
    if (record) result.processes.push_back({currentStep, spec});
    if (launched) *launched = ProcessRecord{currentStep,spec};
    auto run = processTask([&] { return runner(spec); });
    return checkedProcess(std::move(run), spec, processDeadline, currentStep, compilation);
  }
  static Result<ProcessResult> checkedProcess(Result<ProcessResult> run, const ProcessSpec& spec,
                                       Clock::time_point processDeadline, const std::string& processStep, bool compilation = true) {
    if (!run.ok()) return run.error();
    const auto& r = run.value();
    if (Clock::now() >= processDeadline || r.timedOut) return Error{processStep + ": " + (compilation ? "step" : "run") + " deadline exceeded in " + pathToUtf8(spec.cwd)};
    if (r.stdoutTruncated || r.stderrTruncated) return Error{processStep + ": process output cap exceeded in " + pathToUtf8(spec.cwd)};
    if (r.signal || (compilation && r.exitCode)) return Error{processStep + ": process " + (spec.argv.empty() ? "<empty argv>" : spec.argv.front()) + " failed (exit " + std::to_string(r.exitCode) + ") in " + pathToUtf8(spec.cwd) + ": " + r.stderrText};
    return run;
  }
  std::vector<ProcessAttempt> limitedBatch(const std::vector<ProcessSpec>& inputs, bool preprocessing = false) {
    std::vector<ProcessAttempt> attempts(inputs.size());
    std::atomic<std::size_t> next{0};
    const auto jobs = availableJobs();
    // Each worker owns one result slot. limited() captures the effective spec
    // at launch; only the caller publishes records, in consumption order.
    auto work = [&] {
      for (;;) {
        const auto i = next.fetch_add(1);
        if (i >= inputs.size()) return;
        auto& attempt = attempts[i];
        attempt.outcome = limited(inputs[i],true,preprocessing,false,&attempt.record);
        if (jobs == 1 && !attempt.outcome.ok()) return;
      }
    };
    std::vector<std::future<void>> workers;
    for (std::size_t i = 1; i < std::min<std::size_t>(jobs,inputs.size()); ++i)
      workers.push_back(std::async(std::launch::async,work));
    work();
    for (auto& worker : workers) worker.get();
    return attempts;
  }
  ProbeOptions probeOptions() {
    ProbeOptions po; po.temporaryRoot = work.path;
    po.timeout = options.stepTimeout; po.maxOutputBytes = options.maxProcessOutput;
    po.runner = [this](const ProcessSpec& s) { return limited(s); };
    if (options.jobs > 1) {
      auto pending = std::make_shared<std::vector<ProcessAttempt>>();
      po.batchRunner = [this,pending](const std::vector<ProcessSpec>& specs) {
        *pending = limitedBatch(specs);
        std::vector<Result<ProcessResult>> outcomes;
        for (auto& attempt : *pending) outcomes.push_back(std::move(attempt.outcome));
        return outcomes;
      };
      po.batchConsumed = [this,pending,next = std::size_t{}](const ProcessSpec&) mutable {
        auto& attempt = pending->at(next++);
        if (attempt.record) result.processes.push_back(std::move(*attempt.record));
      };
    }
    return po;
  }
  Configuration configuration(CompileCommand c, const std::string& kind) {
    // Generated builds execute away from the input tree. Keep system/library
    // search directories meaningful when changing cwd; user paths are stripped.
    for (auto& a : c.args) if (a.option == "-isystem" || a.option == "-L") {
      a.value = pathToUtf8(c.cwd / pathFromUtf8(a.value)); a.spelling = {a.option, a.value};
    }
    auto po = probeOptions();
    if (!independentCompilerCommand(c)) { po.batchRunner = {}; po.batchConsumed = {}; }
    return take(probeConfiguration(std::move(c), kind, po));
  }
  std::vector<std::string> argv(const Configuration& c, const std::filesystem::path& file,
                                const std::filesystem::path& output, bool generated) {
    auto args = c.command.buildArgv(file, output, true, {}, generated);
    if (!generated && c.command.userIncludePaths().empty())
      for (const auto& a : command.args) if (a.option == "-I" || a.option == "-iquote")
        args.insert(args.end(),a.spelling.begin(),a.spelling.end());
    return args;
  }
  std::filesystem::path build(const Configuration& c, const std::filesystem::path& file, bool generated, std::string_view name) {
    ProcessSpec spec;
    const auto program = work.path / (std::string(name) + ".exe");
    std::filesystem::remove(program);
    spec.argv = argv(c, file, program, generated);
    spec.cwd = generated ? work.path : command.cwd;
    spec.timeout = options.stepTimeout;
    take(limited(spec));
    if (!std::filesystem::is_regular_file(program)) throw Error{"compiler did not produce an executable for " + pathToUtf8(file)};
    return program;
  }
  void startBaseline(const Configuration& c) {
    beforeBaseline = result; baselineConfiguration = c;
    const auto program = work.path / "baseline.exe";
    std::filesystem::remove(program);
    ProcessSpec spec; spec.argv = argv(c,command.source,program,false); spec.cwd = command.cwd;
    spec.timeout = (std::min)(options.stepTimeout,std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()));
    if (spec.timeout.count() <= 0) throw Error{currentStep + ": step deadline exceeded in " + pathToUtf8(spec.cwd)};
    spec.maxOutputBytes = (std::min)(spec.maxOutputBytes,options.maxProcessOutput);
    baselineRecord = {currentStep,spec}; result.processes.push_back(baselineRecord);
    const auto phaseDeadline = deadline;
    baselineBuild = std::async(std::launch::async,[spec,program,source = command.source,phaseDeadline,run = runner]() -> Result<ProcessResult> {
      return processTask([&]() -> Result<ProcessResult> {
        auto result = checkedProcess(run(spec),spec,phaseDeadline,"baseline build");
        if (result.ok() && !std::filesystem::is_regular_file(program))
          return Error{"compiler did not produce an executable for " + pathToUtf8(source)};
        return result;
      });
    });
  }
  void finishBaseline() {
    if (!baselineBuild.valid()) return;
    auto built = baselineBuild.get();
    if (!built.ok()) {
      // Earlier failures are authoritative. Discard speculative raw work from
      // the serial report and retain the baseline failure's original wording.
      result = std::move(*beforeBaseline);
      speculativeProgress.clear();
      beforeBaseline.reset();
      result.processes.push_back(baselineRecord);
      result.checks.push_back({"baseline",built.error().message,baselineConfiguration,{}});
      throw Error{"baseline failed: " + built.error().message};
    }
    beforeBaseline.reset(); baselineConfiguration.reset();
    for (const auto& message : speculativeProgress) progress(message);
    speculativeProgress.clear();
  }
  std::vector<ProcessResult> execute(const Configuration& c, const std::filesystem::path& program) {
    if (options.samples.empty()) return {};
    if (!runnable(c)) {
      result.runtime = "skipped: can't run target binaries here"; return {};
    }
    std::vector<ProcessResult> output;
    for (const auto& sample : options.samples) {
      ProcessSpec spec; spec.argv = {pathToUtf8(program)}; spec.cwd = work.path;
      spec.stdinFile = command.cwd / sample.input; spec.timeout = options.stepTimeout;
      auto r = limited(spec,false);
      if (!r.ok()) throw Error{"sample " + sample.name + ": " + r.error().message};
      output.push_back(std::move(r.value()));
    }
    return output;
  }
  void compareRuns(const std::vector<ProcessResult>& reference, const std::vector<ProcessResult>& candidate) {
    if (reference.size() != candidate.size()) throw Error{"sample execution availability differs"};
    for (std::size_t i = 0; i < reference.size(); ++i) {
      result.runtime = "compared " + std::to_string(i + 1) + " samples";
      const bool same = reference[i].exitCode == candidate[i].exitCode && reference[i].stdoutText == candidate[i].stdoutText;
      progress(currentStep + ": sample " + options.samples[i].name + (same ? " matches" : " differs"));
      if (!same)
        throw Error{"differs on sample " + options.samples[i].name + " (distill bug or nondeterministic program)"};
    }
  }
  PruningModel sourceModel(const std::string& text, const std::vector<std::optional<SourceLocation>>& lines,
                           const std::vector<std::size_t>& keep) {
    PruningModel m; m.text = text; m.lineMap = lines; m.keepOffsets = keep; return m;
  }
  PruningModel strip(const PruningModel& m) {
    if (!options.stripComments && !options.shortenNames) return m;
    auto rendered = take(renderSource(m,nullptr,true));
    take(renderingInvariant(m,nullptr,rendered.provenance.text));
    return std::move(rendered.provenance);
  }
  void prune(const InlineResult& raw) {
    if (options.noPrune && options.keep.empty()) {
      result.diagnostic += "pruning disabled: --no-prune\n"; return;
    }
    const auto flatRuntime = result.runtime;
    step("pruning analysis");
    try {
      auto m = take(preparePruning(*flatConfiguration,raw,result.resolution,options.profile,options.keep));
      m.shortenNames = options.shortenNames;
      m.assumeLibrarySpelledNames = options.assumeLibrarySpelledNames;
      m.assumeInstantiationEffects = options.assumeInstantiationEffects;
      m.assumeLibrarySpelledMembers = options.assumeLibrarySpelledMembers;
      m.assumeUnspelledFunctions = options.assumeUnspelledFunctions;
      m.assumeIdempotentStandardHeaders = options.assumeIdempotentStandardHeaders;
      m.assumeUnobservedGuardMacros = options.assumeUnobservedGuardMacros;
      PruneWorkerOptions worker; worker.executable = options.pruningWorker; worker.temporaryRoot = work.path;
      worker.timeout = options.stepTimeout; worker.deadline = deadline;
      worker.runner = [this](const ProcessSpec& spec) -> Result<ProcessResult> {
        result.processes.push_back({currentStep,spec}); return runner(spec);
      };
      if (worker.executable.empty()) m.disabledReasons.push_back("pruning worker executable unavailable");
      else m = take(extractPruning(m,worker));
      result.pruningModel = std::move(m);
      result.retention = take(computeRetention(*result.pruningModel));
      for (const auto& name : options.keep) {
        const auto& families = result.pruningModel->families;
        if (std::none_of(families.begin(),families.end(),[&](const auto& f) {
              return f.names.contains(name) || (f.memberOf != noFamily && f.memberName == name);
            }))
          throw Error{"--keep " + name + ": no indexed declaration in checked flat source"};
      }
      if (Clock::now() >= deadline) throw Error{"pruning analysis deadline exceeded for flat.cpp"};
      for (const auto& why : result.retention->disabledReasons) result.diagnostic += "pruning disabled: " + why + '\n';
      if (!result.retention->disabledReasons.empty()) return;
      if (options.noPrune) { result.diagnostic += "pruning disabled: --no-prune\n"; return; }
      step("render pruned");
      auto rendered = take(renderSource(*result.pruningModel,&*result.retention,options.stripComments || options.shortenNames));
      result.retention->trimming = rendered.trimming;
      const auto& bytes = rendered.provenance.text;
      take(renderingInvariant(*result.pruningModel,&*result.retention,bytes));
      take(staticNameInvariant(*result.pruningModel,*result.retention,bytes));
      if (Clock::now() >= deadline) throw Error{"pruned rendering deadline exceeded"};
      const auto path = work.path / "pruned.cpp"; write(path,bytes);
      step("pruned checks");
      auto program = build(*flatConfiguration,path,true,"pruned");
      compareRuns(runs["flat"],execute(*flatConfiguration,program));
      checkedBytes(path,bytes);
      result.checks.push_back({"pruned","passed",flatConfiguration,{}});
      result.artifact = "pruned"; result.bytes = bytes;
      result.assumptions.insert(result.retention->assumptions.begin(),result.retention->assumptions.end());
      if (options.noLineMarkers || options.compactWhitespace || options.shortenNames) unmark(rendered,path);
    } catch (const Error& e) {
      if (e.message.starts_with("--keep ")) throw;
      result.runtime = flatRuntime;
      result.checks.push_back({"pruned",e.message,flatConfiguration,{}});
      result.diagnostic += "pruned rejected: " + e.message + "; using flat\n";
    }
  }
  std::string markedRuntime;
  void keepMarkers(const std::string& why) {
    if (result.lineMarkers.empty()) result.lineMarkers = "kept: " + why;
    result.diagnostic += "line markers kept: " + why + '\n';
  }
  void skipRenderingBuild(std::string_view stage) {
    result.diagnostic += "verify: " + std::string(stage) + " build skipped (--verify=final)\n";
  }
  // Runs after pruned passed every check; any failure keeps the checked
  // marked bytes (docs/reviews/2026-11-18-no-line-markers.md).
  void unmark(const RenderedSource& rendered, const std::filesystem::path& marked) {
    const auto why = lineMarkerObservations(*result.pruningModel,flatConfiguration->argv);
    if (!why.empty()) {
      const std::size_t shown = std::min<std::size_t>(why.size(),10);
      for (std::size_t i = 0; i < shown; ++i) keepMarkers(why[i]);
      if (why.size() > shown) result.diagnostic += "line markers kept: " + std::to_string(why.size() - shown) + " more observations\n";
      return;
    }
    markedRuntime = result.runtime;
    try {
      step("unmarked checks");
      const auto bytes = take(withoutLineMarkers(rendered));
      take(unmarkedInvariant(rendered.provenance.text,bytes));
      const auto path = work.path / "unmarked.cpp"; write(path,bytes);
      const auto accept = [&] {
        result.lineMarkers = "omitted";
        result.diagnostic += "line markers omitted: no user code observes presumed source positions\n";
      };
      const bool deferred = options.verifyFinal && (options.compactWhitespace || options.shortenNames);
      if (deferred) {
        if (Clock::now() >= deadline) throw Error{"unmarked checks deadline exceeded"};
        skipRenderingBuild("unmarked");
        if (compact(path,bytes)) { accept(); return; }
        // No later rendering passed. Fully check these exact bytes before
        // making them the fallback; otherwise retain the checked pruned bytes.
        step("unmarked checks");
      }
      prefetchPreprocessing(*flatConfiguration,marked,true,*flatConfiguration,path);
      take(compareUnmarkedPreprocessed(preprocess(*flatConfiguration,marked,true),preprocess(*flatConfiguration,path,true)));
      auto program = build(*flatConfiguration,path,true,"unmarked");
      compareRuns(runs["flat"],execute(*flatConfiguration,program));
      checkedBytes(path,bytes); checkedBytes(marked,rendered.provenance.text);
      if (Clock::now() >= deadline) throw Error{"unmarked checks deadline exceeded"};
      result.checks.push_back({"pruned without line markers","passed",flatConfiguration,{}});
      result.bytes = bytes; accept();
      if (!deferred && (options.compactWhitespace || options.shortenNames)) compact(path,bytes);
    } catch (const Error& e) { failedUnmarked(e.message); }
    catch (const std::exception& e) { failedUnmarked(e.what()); }
  }
  void failedUnmarked(const std::string& message) {
    result.runtime = markedRuntime;
    result.checks.push_back({"pruned without line markers",message,flatConfiguration,{}});
    keepMarkers("marker-free check failed: " + message);
  }
  void keepWhitespace(const std::string& why) {
    result.compactWhitespace = "kept: " + why;
    result.diagnostic += "whitespace kept: " + why + '\n';
  }
  // Return whether this rendering or a successor passed all of its checks.
  // Deferred ancestors never replace result.bytes until they pass themselves.
  bool compact(const std::filesystem::path& unmarked, const std::string& original) {
    const auto previousRuntime = result.runtime;
    try {
      step("compact checks");
      const auto bytes = take(compactWhitespace(original));
      take(unmarkedInvariant(original,bytes));
      const auto path = work.path / "compact.cpp"; write(path,bytes);
      const auto accept = [&] {
        result.compactWhitespace = "compacted";
        result.diagnostic += "whitespace compacted: no user code observes source positions (including columns)\n";
      };
      const bool deferred = options.verifyFinal && options.shortenNames;
      if (deferred) {
        if (Clock::now() >= deadline) throw Error{"compact checks deadline exceeded"};
        skipRenderingBuild("compact");
        if (shortenNames(bytes)) { accept(); return true; }
        step("compact checks");
      }
      prefetchPreprocessing(*flatConfiguration,unmarked,true,*flatConfiguration,path);
      take(compareUnmarkedPreprocessed(preprocess(*flatConfiguration,unmarked,true),preprocess(*flatConfiguration,path,true)));
      auto program = build(*flatConfiguration,path,true,"compact");
      compareRuns(runs["flat"],execute(*flatConfiguration,program));
      checkedBytes(path,bytes); checkedBytes(unmarked,original);
      if (Clock::now() >= deadline) throw Error{"compact checks deadline exceeded"};
      result.checks.push_back({"pruned with compact whitespace","passed",flatConfiguration,{}});
      result.bytes = bytes; accept();
      if (!deferred && options.shortenNames) shortenNames(bytes);
      return true;
    } catch (const Error& e) { failedCompact(e.message,previousRuntime); }
    catch (const std::exception& e) { failedCompact(e.what(),previousRuntime); }
    return false;
  }
  void keepShortenNames(const std::string& why) {
    result.shortenNames = "kept: " + why;
    result.diagnostic += "shorten-names: " + result.shortenNames + '\n';
  }
  bool shortenNames(const std::string& original) {
    const auto previousRuntime = result.runtime;
    try {
      step("shorten-names checks");
      ResolveResult compactSource; compactSource.text = original;
      auto input = take(preparePruning(*flatConfiguration,{},compactSource,options.profile,{}));
      input.shortenNames = true;
      input.assumeLibrarySpelledNames = options.assumeLibrarySpelledNames;
      input.assumeLibrarySpelledMembers = options.assumeLibrarySpelledMembers;
      input.assumeInstantiationEffects = options.assumeInstantiationEffects;
      input.assumeUnspelledFunctions = options.assumeUnspelledFunctions;
      input.assumeIdempotentStandardHeaders = options.assumeIdempotentStandardHeaders;
      input.assumeUnobservedGuardMacros = options.assumeUnobservedGuardMacros;
      PruneWorkerOptions worker; worker.executable = options.pruningWorker; worker.temporaryRoot = work.path;
      worker.timeout = options.stepTimeout; worker.deadline = deadline;
      worker.runner = [this](const ProcessSpec& spec) -> Result<ProcessResult> {
        result.processes.push_back({currentStep,spec}); return runner(spec);
      };
      const auto model = take(extractPruning(input,worker));
      if (!model.disabledReasons.empty()) throw Error{"shorten-names analysis: " + model.disabledReasons.front()};
      const auto candidate = take(shortenNamesSource(original,model));
      take(shortenNamesInvariant(original,candidate.text,candidate.renames));
      if (!candidate.namesKept.empty()) { keepShortenNames(candidate.namesKept); return false; }
      const auto path = work.path / "shortened.cpp"; write(path,candidate.text);
      auto program = build(*flatConfiguration,path,true,"shortened");
      compareRuns(runs["flat"],execute(*flatConfiguration,program));
      checkedBytes(path,candidate.text);
      if (Clock::now() >= deadline) throw Error{"shorten-names checks deadline exceeded"};
      if (candidate.text.size() > original.size()) throw Error{"candidate grew"};
      result.checks.push_back({"pruned with shortened names","passed",flatConfiguration,{}});
      result.bytes = candidate.text;
      result.shortenNames = "renamed=" + std::to_string(candidate.renames.size()) + " spellings saved=" + std::to_string(original.size()-candidate.text.size()) + " bytes";
      result.diagnostic += "shorten-names: " + result.shortenNames + '\n';
      if (!candidate.renames.empty()) result.assumptions.insert("A15");
      return true;
    } catch (const Error& e) {
      result.runtime = previousRuntime;
      result.checks.push_back({"pruned with shortened names",e.message,flatConfiguration,{}});
      keepShortenNames(e.message);
    } catch (const std::exception& e) { result.runtime = previousRuntime; keepShortenNames(e.what()); }
    return false;
  }
  void failedCompact(const std::string& message, const std::string& previousRuntime) {
    result.runtime = previousRuntime;
    result.checks.push_back({"pruned with compact whitespace",message,flatConfiguration,{}});
    keepWhitespace("compact check failed: " + message);
  }
  ProcessSpec preprocessSpec(const Configuration& c, const std::filesystem::path& file, bool generated) {
    ProcessSpec spec; spec.argv = argv(c, file, "-", generated); spec.argv.push_back("-E");
    spec.cwd = generated ? work.path : command.cwd;
    spec.timeout = options.stepTimeout; spec.maxOutputBytes = options.maxPreprocessOutput;
    return spec;
  }
  void prefetchPreprocessing(const Configuration& a, const std::filesystem::path& left, bool generated,
                             const Configuration& b, const std::filesystem::path& right) {
    if (availableJobs() < 2 || !independentCompilerCommand(a.command) || !independentCompilerCommand(b.command)) return;
    // Prefetch without publishing records. The original call expressions below
    // consume results and errors in exactly their existing evaluation order.
    auto results = limitedBatch({preprocessSpec(a,left,generated),preprocessSpec(b,right,true)},true);
    preprocessed.insert_or_assign(left,std::move(results[0]));
    preprocessed.insert_or_assign(right,std::move(results[1]));
  }
  std::string preprocess(const Configuration& c, const std::filesystem::path& file, bool generated) {
    auto pending = preprocessed.extract(file);
    if (!pending.empty()) {
      auto& attempt = pending.mapped();
      if (attempt.record) result.processes.push_back(std::move(*attempt.record));
      return std::move(take(std::move(attempt.outcome)).stdoutText);
    }
    return std::move(take(limited(preprocessSpec(c,file,generated),true,true)).stdoutText);
  }
  void compare(const Configuration& c, const std::filesystem::path& a, bool original,
               const std::filesystem::path& b, const InlineResult& raw, const Configuration* baseline = nullptr) {
    prefetchPreprocessing(baseline ? *baseline : c,a,!original,c,b);
    auto left = preprocess(baseline ? *baseline : c, a, !original), right = preprocess(c, b, true);
    take(comparePreprocessed(left, right, originalFileMapper(raw)));
  }
  void changes(Configuration& c) {
    if (!options.profile) return;
    for (const auto& a : command.args) if (a.option == "-D" || a.option == "-U" || a.option == "-std=")
      c.changes.push_back({a.spelling, "replaced by profile dialect/ordered macro operations"});
    if (c.kind == "local") for (const auto& f : options.profile->link)
      c.changes.push_back({{f}, "profile link option omitted for local fallback; local link options retained"});
  }
  PipelineResult run() {
    if (options.verifyFinal) result.optionLines.push_back("verify=final");
    try {
      if (!options.jobs) throw Error{"--jobs requires a positive integer"};
      for (const auto& arg : command.args) if (arg.option == "-shared" || arg.option == "-r")
        throw Error{"compile command does not build an executable: " + arg.option};
      step("probe local compiler");
      command.compiler = take(resolveExecutable(command.compiler, command.cwd));
      auto local = configuration(command, "local");
      if (!options.compilerExplicit && local.probe.identity.family != CompilerFamily::gcc)
        throw Error{"default g++ is not GCC; name a real GCC (e.g. g++-16) or a profile cross compiler explicitly"};
      if (local.probe.identity.family == CompilerFamily::clang || local.probe.identity.family == CompilerFamily::appleClang)
        result.diagnostic += "explicitly named Clang accepted\n";
      if (options.profile && command.dialect() && *command.dialect() != options.profile->dialect.name)
        result.diagnostic += "warning: profile dialect " + options.profile->dialect.name + " overrides " + *command.dialect() + " for flat\n";
      step("baseline build");
      try {
        if (options.jobs > 1 && options.samples.empty() && independentCompilerCommand(command)) startBaseline(local);
        else runs["baseline"] = execute(local,build(local, command.source, false,"baseline"));
      }
      catch (const Error& e) {
        result.checks.push_back({"baseline", e.message, local, {}});
        throw Error{"baseline failed: " + e.message};
      }
      result.checks.push_back({"baseline", "passed", local, {}});
      step("inline");
      InlineOptions io; if (options.profile) io.libraries = options.profile->libraries;
      io.deadline = deadline;
      auto raw = take(inlineSources(command, take(buildIncludeSearch(command, local.probe.searchLists)), io));
      if (Clock::now() >= deadline) throw Error{"inline deadline exceeded"};
      raw.keepOffsets = take(captureKeepOffsets(raw.text));
      auto rawChecked = strip(sourceModel(raw.text,raw.lineMap,raw.keepOffsets));
      if (Clock::now() >= deadline) throw Error{"raw rendering deadline exceeded"};
      const auto rawPath = work.path / "raw.cpp", flatPath = work.path / "flat.cpp";
      write(rawPath, rawChecked.text);
      step("raw checks");
      auto rawLocal = local;
      try {
        rawLocal = configuration(take(flatCommand(command, nullptr, command.compiler, false)), "local");
        compare(rawLocal, command.source, true, rawPath, raw, &local);
        auto rawProgram = build(rawLocal, rawPath, true,"raw");
        compareRuns(runs["baseline"],execute(rawLocal,rawProgram));
        checkedBytes(rawPath, rawChecked.text);
        if (Clock::now() >= deadline) throw Error{"raw checks deadline exceeded"};
      } catch (const Error& e) {
        finishBaseline();
        work.keep = true; result.keptWork = work.path;
        result.checks.push_back({"raw", e.message, rawLocal, {}});
        throw Error{"raw failed: " + e.message + "; work files kept at " + pathToUtf8(work.path)};
      }
      finishBaseline();
      result.checks.push_back({"raw", "passed", rawLocal, {}});
      result.artifact = "raw"; result.bytes = rawChecked.text; result.assumptions = raw.assumptions;
      result.verification = targetEquivalent(rawLocal, options.profile, hostDependent(command)) ? "target" : "local";
      const auto rawRuntime = result.runtime;
      if (options.mergeOnly) result.diagnostic += "resolution disabled: --merge-only\n";
      else try {
        step("resolve conditions");
        ResolveOptions ro; ro.profile = options.profile; ro.deadline = deadline;
        ro.assumeLibraryMacroValues = options.assumeLibraryMacroValues;
        if (!options.profile) ro.requirements.dialect = local.probe.dialect.name;
        // No profile means no local predefined facts: preserve the D2d rule.
        result.resolution = take(resolveConditions(raw, command, evaluator, ro));
        if (Clock::now() >= deadline) throw Error{"resolution deadline exceeded"};
        if (!result.resolution.disabledReasons.empty()) {
          std::string why = "resolution disabled";
          for (const auto& r : result.resolution.disabledReasons) why += ": " + r;
          throw Error{why};
        }
        auto publish = [&](ResolveResult resolution) {
          auto flatChecked = strip(sourceModel(resolution.text,resolution.lineMap,resolution.keepOffsets));
          resolution.text = std::move(flatChecked.text);
          resolution.lineMap = std::move(flatChecked.lineMap);
          resolution.keepOffsets = std::move(flatChecked.keepOffsets);
          if (Clock::now() >= deadline) throw Error{"flat rendering deadline exceeded"};
          write(flatPath, resolution.text);
          result.resolution = std::move(resolution);
        };
        publish(std::move(result.resolution));
        step("select configuration");
        std::vector<std::pair<std::filesystem::path, bool>> candidates;
        if (options.profile) for (const auto& compiler : options.profile->check) {
          auto found = resolveExecutable(pathFromUtf8(compiler), command.cwd);
          if (found.ok()) candidates.emplace_back(found.value(), true);
          else result.diagnostic += "check compiler unavailable: " + compiler + '\n';
        }
        candidates.emplace_back(command.compiler, false);
        auto checkFlat = [&](const Configuration& c, const ResolveResult& observed) {
          step("flat checks");
          try {
            compare(c, rawPath, false, flatPath, raw);
            if (!options.samples.empty()) runs["original flat configuration"] = execute(c,build(c,command.source,false,"original-flat"));
            const auto flatProgram = build(c,flatPath,true,"flat");
            runs["flat"] = execute(c,flatProgram);
            compareRuns(runs["original flat configuration"],runs["flat"]);
            checkedBytes(rawPath, rawChecked.text); checkedBytes(flatPath, result.resolution.text);
            if (Clock::now() >= deadline) throw Error{"flat checks deadline exceeded"};
            result.checks.push_back({"flat", "passed", c, observed.decisions});
            result.artifact = "flat"; result.bytes = result.resolution.text; flatConfiguration = c;
            for (const auto& d : result.resolution.decisions) if (d.decision == Decision::trueValue || d.decision == Decision::falseValue) {
              if (options.profile && (d.requirements.integers || d.requirements.dialect)) result.assumptions.insert("A4");
              for (const auto& u : d.external) {
                for (const auto origin : u.fact.provenance) {
                  if (origin == FactOrigin::a3) result.assumptions.insert("A3");
                  if (origin == FactOrigin::a5) result.assumptions.insert("A5");
                  if (origin == FactOrigin::library) result.assumptions.insert("A9");
                }
                if (options.profile && !u.fact.provenance.empty()) result.assumptions.insert("A4");
              }
              if (std::any_of(raw.systemIncludes.begin(), raw.systemIncludes.end(), [](const auto& i) { return i.standard; })) {
                result.assumptions.insert("A1"); result.assumptions.insert("A2");
              }
            }
            result.verification = targetEquivalent(c, options.profile, hostDependent(command)) ? "target" : "local";
          } catch (const Error& e) {
            result.checks.push_back({"flat", e.message, c, observed.decisions});
            result.diagnostic += "flat rejected: " + e.message + "; using raw\n";
          }
        };
        bool selected = false;
        std::vector<std::pair<Configuration, ResolveResult>> disagreeing;
        for (const auto& [compiler, check] : candidates) {
          if (Clock::now() >= deadline) { result.diagnostic += "configuration selection deadline exceeded\n"; break; }
          try {
            auto c = configuration(take(flatCommand(command, options.profile, compiler, check)), check ? "check" : "local");
            if (check && !compatibleProfileTriple(options.profile->triple, c.probe.triple)) {
              const auto why = "check compiler skipped: " + pathToUtf8(compiler) + ": target triple " +
                  c.probe.triple + " is incompatible with judge target " + options.profile->triple;
              result.diagnostic += why + '\n';
              result.checks.push_back({"flat", why, c, {}});
              continue;
            }
            changes(c);
            ResolveResult observed;
            auto satisfied = take(satisfies(c, raw, command, result.resolution, evaluator, deadline, &observed,
                                            options.assumeLibraryMacroValues));
            if (Clock::now() >= deadline) throw Error{"configuration satisfaction deadline exceeded"};
            if (!satisfied) {
              result.checks.push_back({"flat", "configuration does not satisfy resolution", c, observed.decisions});
              disagreeing.emplace_back(std::move(c), std::move(observed)); continue;
            }
            selected = true;
            checkFlat(c, observed);
            break; // The first satisfying configuration is authoritative, even on failure.
          } catch (const Error& e) { result.diagnostic += "configuration " + pathToUtf8(compiler) + ": " + e.message + '\n'; }
        }
        // Only when nothing satisfies as is: keep exactly the conditionals a
        // candidate disagrees with unknown, so its own compiler evaluates them
        // (docs/reviews/2026-11-18-cross-host-demotion.md).
        for (const auto& [c, observed] : disagreeing) {
          if (selected) break;
          if (Clock::now() >= deadline) { result.diagnostic += "demotion deadline exceeded\n"; break; }
          auto demotion = ro; demotion.deadline = deadline;
          std::vector<ResolutionDecision> demoted;
          auto demotedResolution = demoteDisagreements(raw, command, evaluator, demotion, result.resolution, observed, &demoted);
          if (!demotedResolution.ok()) {
            result.diagnostic += "configuration " + pathToUtf8(c.command.compiler) + ": no demotion: " + demotedResolution.error().message + '\n';
            continue;
          }
          publish(std::move(demotedResolution.value()));
          for (const auto& d : demoted) {
            result.diagnostic += "flat: demoted ";
            if (d.original) result.diagnostic += d.original->file + ':' + std::to_string(d.original->line);
            else result.diagnostic += "<generated>:" + std::to_string(d.rawLine);
            result.diagnostic += " (#" + d.directive + "): " + c.kind + " configuration disagrees\n";
          }
          selected = true;
          checkFlat(c, observed);
          break;
        }
        if (!selected) throw Error{"no configuration satisfies resolution; using raw"};
      } catch (const Error& e) { result.diagnostic += e.message + '\n'; }
      if (result.artifact == "raw") result.runtime = rawRuntime;
      if (result.artifact == "flat") prune(raw);
      else if (!options.keep.empty()) throw Error{"--keep " + options.keep.front() + ": requires a checked flat configuration"};
      if ((options.noLineMarkers || options.compactWhitespace || options.shortenNames) && result.lineMarkers.empty())
        keepMarkers(result.pruningModel ? "no checked pruned artifact" : "no parse");
      if ((options.compactWhitespace || options.shortenNames) && result.compactWhitespace.empty())
        keepWhitespace(result.lineMarkers.starts_with("kept: ") ? result.lineMarkers.substr(6) : "no checked pruned artifact");
      if (options.shortenNames && result.shortenNames.empty()) {
        if (result.pruningModel && result.pruningModel->shorteningRefused.starts_with("observable names:"))
          keepShortenNames(result.pruningModel->shorteningRefused);
        else keepShortenNames(result.compactWhitespace.starts_with("kept: ") ? result.compactWhitespace.substr(6) : "no checked compact artifact");
      }
      if (result.verification == "target") result.assumptions.insert("A4");
      result.exitCode = options.strict && (result.artifact != "pruned" || result.verification != "target") ? 3 : 0;
      result.resultLine = "distill: wrote " + result.artifact + " (" + result.verification + "), runtime " + result.runtime;
    } catch (const Error& e) {
      auto failure = e;
      try { finishBaseline(); } catch (const Error& baselineError) { failure = baselineError; }
      result.exitCode = 1; result.bytes.clear(); result.artifact.clear();
      result.diagnostic += failure.message + '\n';
      result.resultLine = "distill: nothing written, runtime " + result.runtime;
    }
    if (!options.assumeInstantiationEffects && (!result.pruningModel || !result.retention))
      result.diagnostic += "pruning assumption disabled: A11 instantiation-effects (--no-assume=instantiation-effects)\n";
    if (!options.assumeLibrarySpelledMembers && (!result.pruningModel || !result.retention))
      result.diagnostic += "pruning assumption disabled: A12 library-spelled-members (--no-assume=library-spelled-members)\n";
    if (!options.assumeUnspelledFunctions && (!result.pruningModel || !result.retention))
      result.diagnostic += "pruning assumption disabled: D3f unspelled-functions (--no-assume=unspelled-functions)\n";
    if (!options.assumeIdempotentStandardHeaders && (!result.pruningModel || !result.retention))
      result.diagnostic += "pruning policy disabled: idempotent-standard-headers (--no-assume=idempotent-standard-headers)\n";
    if (!options.assumeUnobservedGuardMacros && (!result.pruningModel || !result.retention))
      result.diagnostic += "pruning assumption disabled: A14 unobserved-guard-macros (--no-assume=unobserved-guard-macros)\n";
    if (!options.assumeLibrarySpelledNames)
      result.diagnostic += "shorten-names assumption disabled: A15 library-spelled-names (--no-assume=library-spelled-names)\n";
    if (!options.assumeLibraryMacroValues)
      result.diagnostic += "resolution assumption disabled: A9 library-macro-values (--no-assume=library-macro-values); __cpp_lib_* facts retained\n";
    result.report = renderReport(result, command, options.profile);
    return std::move(result);
  }
};
} // namespace
Result<std::vector<RunSample>> collectRunSamples(const std::vector<std::string>& paths,
                                                const std::filesystem::path& cwd) {
  try {
    std::vector<RunSample> samples;
    for (const auto& name : paths) {
      const auto path = cwd / pathFromUtf8(name);
      if (std::filesystem::is_regular_file(path)) samples.push_back({name,path});
      else if (std::filesystem::is_directory(path)) {
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(path))
          if (entry.is_regular_file() && entry.path().extension() == ".in") files.push_back(entry.path());
        std::sort(files.begin(),files.end());
        if (files.empty()) return Error{"--verify-run " + name + ": directory has no *.in samples"};
        for (const auto& file : files) samples.push_back({pathToUtf8(pathFromUtf8(name) / file.filename()),file});
      } else return Error{"--verify-run " + name + ": not a sample file or directory"};
    }
    return samples;
  } catch (const std::exception& e) { return Error{std::string("--verify-run: ") + e.what()}; }
}
Result<PipelineResult> distillProgram(const CompileCommand& command, const DistillOptions& options,
                                    ConditionEvaluator& evaluator, ProcessRunner runner) {
  try { return Pipeline(command, options, evaluator, std::move(runner)).run(); }
  catch (const Error& e) { return e; }
  catch (const std::exception& e) { return Error{std::string("pipeline: ") + e.what()}; }
}
Result<PipelineResult> distillNoPrune(const CompileCommand& command, const DistillOptions& options,
                                    ConditionEvaluator& evaluator, ProcessRunner runner) {
  auto o = options; o.noPrune = true;
  return distillProgram(command,o,evaluator,std::move(runner));
}
} // namespace distill
