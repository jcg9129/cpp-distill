// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/pipeline/pipeline.h"
#include "core/report/report.h"
#include "core/render/render.h"
#include "core/validate/preprocess.h"
#include "core/path.h"
#include "core/temporary.h"
#include <algorithm>
#include <fstream>

namespace distill {
namespace {
template<class T> T take(Result<T> r) { if (!r.ok()) throw r.error(); return std::move(r.value()); }
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
  Pipeline(const CompileCommand& c, const DistillOptions& o, ConditionEvaluator& e, ProcessRunner r)
      : command(c), options(o), evaluator(e), runner(std::move(r)), work(".distill-work-", o.temporaryRoot) {}
  void step(std::string_view name) {
    deadline = Clock::now() + options.stepTimeout;
    currentStep = name;
    if (options.progress) options.progress(name);
  }
  Result<ProcessResult> limited(const ProcessSpec& input, bool compilation = true, bool preprocessing = false) {
    auto spec = input;
    // Both the per-run cap and the enclosing phase deadline apply (§3/§4.6).
    const auto processDeadline = compilation ? deadline : std::min(deadline,Clock::now() + options.stepTimeout);
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(processDeadline - Clock::now());
    if (left.count() <= 0) return Error{currentStep + ": step deadline exceeded in " + pathToUtf8(spec.cwd)};
    spec.timeout = std::min(spec.timeout, left);
    spec.maxOutputBytes = std::min(spec.maxOutputBytes, preprocessing ? options.maxPreprocessOutput : options.maxProcessOutput);
    result.processes.push_back({currentStep, spec});
    auto run = runner(spec);
    if (!run.ok()) return run.error();
    const auto& r = run.value();
    if (Clock::now() >= processDeadline || r.timedOut) return Error{currentStep + ": " + (compilation ? "step" : "run") + " deadline exceeded in " + pathToUtf8(spec.cwd)};
    if (r.stdoutTruncated || r.stderrTruncated) return Error{currentStep + ": process output cap exceeded in " + pathToUtf8(spec.cwd)};
    if (r.signal || (compilation && r.exitCode)) return Error{currentStep + ": process " + (spec.argv.empty() ? "<empty argv>" : spec.argv.front()) + " failed (exit " + std::to_string(r.exitCode) + ") in " + pathToUtf8(spec.cwd) + ": " + r.stderrText};
    return run;
  }
  ProbeOptions probeOptions() {
    ProbeOptions po; po.temporaryRoot = work.path;
    po.timeout = options.stepTimeout; po.maxOutputBytes = options.maxProcessOutput;
    po.runner = [this](const ProcessSpec& s) { return limited(s); }; return po;
  }
  Configuration configuration(CompileCommand c, const std::string& kind) {
    // Generated builds execute away from the input tree. Keep system/library
    // search directories meaningful when changing cwd; user paths are stripped.
    for (auto& a : c.args) if (a.option == "-isystem" || a.option == "-L") {
      a.value = pathToUtf8(c.cwd / pathFromUtf8(a.value)); a.spelling = {a.option, a.value};
    }
    return take(probeConfiguration(std::move(c), kind, probeOptions()));
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
      if (options.progress) options.progress(currentStep + ": sample " + options.samples[i].name + (same ? " matches" : " differs"));
      if (!same)
        throw Error{"differs on sample " + options.samples[i].name + " (distill bug or nondeterministic program)"};
    }
  }
  PruningModel sourceModel(const std::string& text, const std::vector<std::optional<SourceLocation>>& lines,
                           const std::vector<std::size_t>& keep) {
    PruningModel m; m.text = text; m.lineMap = lines; m.keepOffsets = keep; return m;
  }
  PruningModel strip(const PruningModel& m) {
    if (!options.stripComments) return m;
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
        if (std::none_of(families.begin(),families.end(),[&](const auto& f) { return f.names.contains(name); }))
          throw Error{"--keep " + name + ": no indexed declaration in checked flat source"};
      }
      if (Clock::now() >= deadline) throw Error{"pruning analysis deadline exceeded for flat.cpp"};
      for (const auto& why : result.retention->disabledReasons) result.diagnostic += "pruning disabled: " + why + '\n';
      if (!result.retention->disabledReasons.empty()) return;
      if (options.noPrune) { result.diagnostic += "pruning disabled: --no-prune\n"; return; }
      step("render pruned");
      auto rendered = take(renderSource(*result.pruningModel,&*result.retention,options.stripComments));
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
    } catch (const Error& e) {
      if (e.message.starts_with("--keep ")) throw;
      result.runtime = flatRuntime;
      result.checks.push_back({"pruned",e.message,flatConfiguration,{}});
      result.diagnostic += "pruned rejected: " + e.message + "; using flat\n";
    }
  }
  std::string preprocess(const Configuration& c, const std::filesystem::path& file, bool generated) {
    ProcessSpec spec; spec.argv = argv(c, file, "-", generated); spec.argv.push_back("-E");
    spec.cwd = generated ? work.path : command.cwd;
    spec.timeout = options.stepTimeout; spec.maxOutputBytes = options.maxPreprocessOutput;
    return std::move(take(limited(spec,true,true)).stdoutText);
  }
  void compare(const Configuration& c, const std::filesystem::path& a, bool original,
               const std::filesystem::path& b, const InlineResult& raw, const Configuration* baseline = nullptr) {
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
    try {
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
      try { runs["baseline"] = execute(local,build(local, command.source, false,"baseline")); }
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
        compareRuns(runs["baseline"],execute(rawLocal,build(rawLocal, rawPath, true,"raw")));
        checkedBytes(rawPath, rawChecked.text);
        if (Clock::now() >= deadline) throw Error{"raw checks deadline exceeded"};
      } catch (const Error& e) {
        work.keep = true; result.keptWork = work.path;
        result.checks.push_back({"raw", e.message, rawLocal, {}});
        throw Error{"raw failed: " + e.message + "; work files kept at " + pathToUtf8(work.path)};
      }
      result.checks.push_back({"raw", "passed", rawLocal, {}});
      result.artifact = "raw"; result.bytes = rawChecked.text; result.assumptions = raw.assumptions;
      result.verification = targetEquivalent(rawLocal, options.profile, hostDependent(command)) ? "target" : "local";
      const auto rawRuntime = result.runtime;
      try {
        step("resolve conditions");
        ResolveOptions ro; ro.profile = options.profile; ro.deadline = deadline;
        if (!options.profile) ro.requirements.dialect = local.probe.dialect.name;
        // No profile means no local predefined facts: preserve the D2d rule.
        result.resolution = take(resolveConditions(raw, command, evaluator, ro));
        if (Clock::now() >= deadline) throw Error{"resolution deadline exceeded"};
        if (!result.resolution.disabledReasons.empty()) {
          std::string why = "resolution disabled";
          for (const auto& r : result.resolution.disabledReasons) why += ": " + r;
          throw Error{why};
        }
        auto flatChecked = strip(sourceModel(result.resolution.text,result.resolution.lineMap,result.resolution.keepOffsets));
        result.resolution.text = std::move(flatChecked.text);
        result.resolution.lineMap = std::move(flatChecked.lineMap);
        result.resolution.keepOffsets = std::move(flatChecked.keepOffsets);
        if (Clock::now() >= deadline) throw Error{"flat rendering deadline exceeded"};
        write(flatPath, result.resolution.text);
        step("select configuration");
        std::vector<std::pair<std::filesystem::path, bool>> candidates;
        if (options.profile) for (const auto& compiler : options.profile->check) {
          auto found = resolveExecutable(pathFromUtf8(compiler), command.cwd);
          if (found.ok()) candidates.emplace_back(found.value(), true);
          else result.diagnostic += "check compiler unavailable: " + compiler + '\n';
        }
        candidates.emplace_back(command.compiler, false);
        bool selected = false;
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
            auto satisfied = take(satisfies(c, raw, command, result.resolution, evaluator, deadline, &observed));
            if (Clock::now() >= deadline) throw Error{"configuration satisfaction deadline exceeded"};
            if (!satisfied) {
              result.checks.push_back({"flat", "configuration does not satisfy resolution", c, observed.decisions}); continue;
            }
            selected = true;
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
            break; // The first satisfying configuration is authoritative, even on failure.
          } catch (const Error& e) { result.diagnostic += "configuration " + pathToUtf8(compiler) + ": " + e.message + '\n'; }
        }
        if (!selected) throw Error{"no configuration satisfies resolution; using raw"};
      } catch (const Error& e) { result.diagnostic += e.message + '\n'; }
      if (result.artifact == "raw") result.runtime = rawRuntime;
      if (result.artifact == "flat") prune(raw);
      else if (!options.keep.empty()) throw Error{"--keep " + options.keep.front() + ": requires a checked flat configuration"};
      if (result.verification == "target") result.assumptions.insert("A4");
      result.exitCode = options.strict && (result.artifact != "pruned" || result.verification != "target") ? 3 : 0;
      result.resultLine = "distill: wrote " + result.artifact + " (" + result.verification + "), runtime " + result.runtime;
    } catch (const Error& e) {
      result.exitCode = 1; result.bytes.clear(); result.artifact.clear();
      result.diagnostic += e.message + '\n';
      result.resultLine = "distill: nothing written, runtime " + result.runtime;
    }
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
