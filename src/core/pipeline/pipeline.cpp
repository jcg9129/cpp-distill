// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/pipeline/pipeline.h"
#include "core/report/report.h"
#include "core/validate/preprocess.h"
#include "core/path.h"
#include "core/temporary.h"
#include <algorithm>
#include <fstream>

namespace distill {
namespace {
template<class T> T take(Result<T> r) { if (!r.ok()) throw r.error(); return std::move(r.value()); }
using Clock = std::chrono::steady_clock;
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
  Pipeline(const CompileCommand& c, const DistillOptions& o, ConditionEvaluator& e, ProcessRunner r)
      : command(c), options(o), evaluator(e), runner(std::move(r)), work(".distill-work-", o.temporaryRoot) {}
  void step(std::string_view name) {
    deadline = Clock::now() + options.stepTimeout;
    currentStep = name;
    if (options.progress) options.progress(name);
  }
  Result<ProcessResult> limited(const ProcessSpec& input) {
    auto spec = input;
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
    if (left.count() <= 0) return Error{"step deadline exceeded"};
    spec.timeout = std::min(spec.timeout, left);
    spec.maxOutputBytes = std::min(spec.maxOutputBytes, options.maxProcessOutput);
    result.processes.push_back({currentStep, spec});
    auto run = runner(spec);
    if (!run.ok()) return run.error();
    const auto& r = run.value();
    if (Clock::now() >= deadline || r.timedOut) return Error{"step deadline exceeded"};
    if (r.stdoutTruncated || r.stderrTruncated) return Error{"process output cap exceeded"};
    if (r.exitCode || r.signal) return Error{"compiler failed: " + r.stderrText};
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
    return c.command.buildArgv(file, output, true, {}, generated);
  }
  void build(const Configuration& c, const std::filesystem::path& file, bool generated) {
    ProcessSpec spec;
    const auto program = work.path / "program.exe";
    std::filesystem::remove(program);
    spec.argv = argv(c, file, program, generated);
    spec.cwd = generated ? work.path : command.cwd;
    spec.timeout = options.stepTimeout;
    take(limited(spec));
    if (!std::filesystem::is_regular_file(program)) throw Error{"compiler did not produce an executable"};
  }
  std::string preprocess(const Configuration& c, const std::filesystem::path& file, bool generated) {
    ProcessSpec spec; spec.argv = argv(c, file, "-", generated); spec.argv.push_back("-E");
    spec.cwd = generated ? work.path : command.cwd;
    spec.timeout = options.stepTimeout; spec.maxOutputBytes = options.maxProcessOutput;
    return take(limited(spec)).stdoutText;
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
      try { build(local, command.source, false); }
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
      const auto rawPath = work.path / "raw.cpp", flatPath = work.path / "flat.cpp";
      write(rawPath, raw.text);
      step("raw checks");
      auto rawLocal = local;
      try {
        rawLocal = configuration(take(flatCommand(command, nullptr, command.compiler, false)), "local");
        compare(rawLocal, command.source, true, rawPath, raw, &local);
        build(rawLocal, rawPath, true);
        checkedBytes(rawPath, raw.text);
        if (Clock::now() >= deadline) throw Error{"raw checks deadline exceeded"};
      } catch (const Error& e) {
        work.keep = true; result.keptWork = work.path;
        result.checks.push_back({"raw", e.message, rawLocal, {}});
        throw Error{"raw failed: " + e.message + "; work files kept at " + pathToUtf8(work.path)};
      }
      result.checks.push_back({"raw", "passed", rawLocal, {}});
      result.artifact = "raw"; result.bytes = raw.text; result.assumptions = raw.assumptions;
      result.verification = targetEquivalent(rawLocal, options.profile, hostDependent(command)) ? "target" : "local";
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
              compare(c, rawPath, false, flatPath, raw); build(c, flatPath, true);
              checkedBytes(rawPath, raw.text); checkedBytes(flatPath, result.resolution.text);
              if (Clock::now() >= deadline) throw Error{"flat checks deadline exceeded"};
              result.checks.push_back({"flat", "passed", c, observed.decisions});
              result.artifact = "flat"; result.bytes = result.resolution.text;
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
      if (result.verification == "target") result.assumptions.insert("A4");
      result.exitCode = options.strict ? 3 : 0;
      result.resultLine = "distill: wrote " + result.artifact + " (" + result.verification + "), runtime " + result.runtime;
    } catch (const Error& e) {
      result.exitCode = 1; result.bytes.clear(); result.artifact.clear();
      result.diagnostic += e.message + '\n';
      result.resultLine = "distill: nothing written, runtime not requested";
    }
    result.report = renderReport(result, command, options.profile);
    return std::move(result);
  }
};
} // namespace
Result<PipelineResult> distillNoPrune(const CompileCommand& command, const DistillOptions& options,
                                    ConditionEvaluator& evaluator, ProcessRunner runner) {
  try { return Pipeline(command, options, evaluator, std::move(runner)).run(); }
  catch (const Error& e) { return e; }
  catch (const std::exception& e) { return Error{std::string("pipeline: ") + e.what()}; }
}
} // namespace distill
