// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.h"
#include "core/pipeline/pipeline.h"
#include "core/validate/preprocess.h"
#include "evaluate.h"
#include "profile.h"
#include <algorithm>
#include <memory>

using namespace distill;
namespace {
template<class T> T take(Result<T> r) { if (!r.ok()) throw std::runtime_error(r.error().message); return std::move(r.value()); }
struct Fixture {
  test::TempDirectory temp;
  std::filesystem::path gcc;
  std::unique_ptr<ConditionEvaluator> evaluator;
  CompileCommand command;
  DistillOptions options;
  bool strippedKeepObserved = false;
  bool constantExpansionObserved = false;
  std::map<std::string,std::string> built;
  explicit Fixture(std::string text) {
    const auto compiler = testing::testGcc(); if (!compiler) throw test::Skip{"GCC unavailable"}; gcc = *compiler;
    temp.write("main.cpp",text); temp.write("sample.in","");
    evaluator = testing::testEvaluator(temp.path);
    command = take(parseCompileCommand({pathToUtf8(gcc),"-std=gnu++20","main.cpp"},temp.path));
    options.temporaryRoot = temp.path; options.samples = {{"empty sample",temp.path/"sample.in"}};
    // The runner intercepts this private protocol; no worker or LLVM is linked.
    options.pruningWorker = temp.path/"synthetic-worker";
  }
  PipelineResult run(std::string fault = {}) {
    return take(distillProgram(command,options,*evaluator,[&](const ProcessSpec& spec) -> Result<ProcessResult> {
      if (spec.argv.size() == 3 && spec.argv[1] == "--internal-prune") {
        auto m = take(parsePruningModel(test::read(pathFromUtf8(spec.argv[2]))));
        if (m.minify) m.libraryMemberNamesComputed = fault != "minify-library";
        if (fault == "uint64") {
          // Model the real compiler-builtin expansion behind UINT64_C. This
          // must reach retention's predefined-macro judge-fact comparison.
          const auto probe = take(probeCompiler(gcc, command, {true, {}, temp.path}));
          PruneMacroExpansion expansion;
          expansion.offset = m.text.find("UINT64_C(");
          CHECK(expansion.offset != m.text.npos);
          expansion.name = "__UINT64_C"; expansion.predefined = true; expansion.paste = true;
          expansion.parse = {MacroState::known, probe.macros.at(expansion.name), {FactOrigin::compilerBuiltin}};
          expansion.judge = m.judgeInitial.at(expansion.name);
          m.expansions.push_back(expansion); constantExpansionObserved = true;
        }
        auto add = [&](std::string name,std::string_view prefix,std::string_view end) {
          auto begin = m.text.find(prefix); if (begin == m.text.npos) return;
          auto stop = m.text.find(end,begin); if (stop == m.text.npos) throw std::runtime_error("synthetic range missing end");
          stop += end.size();
          PruneFamily f; f.label = name; f.names = {name}; f.callableNames = {name}; f.ranges = {{begin,stop}};
          m.families.push_back(f);
        };
        add("unused","int unused", "}"); add("preserve","int preserve", "}");
        add("chosen","int chosen", "}"); add("call","template<class T>","return 0;}");
        add("main","int main", "}");
        for (auto offset : m.keepOffsets) {
          CHECK(m.text.substr(offset).starts_with("int preserve"));
          CHECK(m.text.find("distill: keep") == m.text.npos); strippedKeepObserved = true;
          for (FamilyId id = 0; id < m.families.size(); ++id) for (auto r : m.families[id].ranges)
            if (r.begin <= offset && offset < r.end) m.annotations.push_back({offset,id});
        }
        if (fault == "disabled") m.disabledReasons.push_back("test-only extraction refusal at main.cpp:1");
        // Model the worker's --no-line-markers observations (rules 1 and 3).
        if (fault == "position") m.positionUses.push_back({m.text.find("__LINE__"),"__LINE__ expanded"});
        if (fault == "default") m.positionUses.push_back({m.text.find("where()"),"call-site default argument with a source position"});
        if (fault == "expansion") {
          PruneMacroExpansion expansion; expansion.offset = m.text.find("__LINE__"); expansion.name = "__LINE__";
          m.expansions.push_back(expansion);
        }
        if (fault == "render") {
          auto& range = m.families.front().ranges.front(); ++range.begin; // Split 'int'.
        }
        ProcessResult r; r.exitCode = 0; r.stdoutText = take(serializePruningModel(m)); return r;
      }
      for (const auto& arg : spec.argv) {
        const auto p = pathFromUtf8(arg);
        if (p.filename() == "raw.cpp" || p.filename() == "flat.cpp" || p.filename() == "pruned.cpp" || p.filename() == "unmarked.cpp" || p.filename() == "compact.cpp" || p.filename() == "minified.cpp")
          built[pathToUtf8(p.stem())] = test::read(p);
      }
      auto result = runProcess(spec);
      const auto program = pathFromUtf8(spec.argv.front()).filename();
      if (result.ok() && fault == "cross" && std::find(spec.argv.begin(),spec.argv.end(),"-dumpmachine") != spec.argv.end())
        result.value().stdoutText = "unrunnable-distill-test-target\n";
      if (result.ok() && spec.argv.size() == 1) {
        if (fault == "stderr") result.value().stderrText = pathToUtf8(program);
        if ((fault == "raw-stdout" && program == "raw.exe") || (fault == "flat-stdout" && program == "flat.exe") ||
            (fault == "unmarked-stdout" && program == "unmarked.exe") || (fault == "compact-stdout" && program == "compact.exe") || (fault == "minify-stdout" && program == "minified.exe"))
          result.value().stdoutText = "different sample output";
        if (fault == "raw-exit" && program == "raw.exe") ++result.value().exitCode;
        if (fault == "raw-timeout" && program == "raw.exe") result.value().timedOut = true;
        if (fault == "raw-cap" && program == "raw.exe") result.value().stdoutTruncated = true;
      }
      if (result.ok() && std::find(spec.argv.begin(),spec.argv.end(),"-E") != spec.argv.end() &&
          std::any_of(spec.argv.begin(),spec.argv.end(),[](const auto& arg) { return pathFromUtf8(arg).filename() == "compact.cpp"; }) && fault == "compact-tokens")
        result.value().stdoutText += "int corrupt;\n";
      if (result.ok() && fault == "compact-build" && built.contains("compact") &&
          std::find(spec.argv.begin(),spec.argv.end(),"-E") == spec.argv.end() && spec.argv.size() > 1)
        result.value().exitCode = 1;
      if (result.ok() && fault == "minify-build" && std::any_of(spec.argv.begin(),spec.argv.end(),[](const auto& arg) { return pathFromUtf8(arg).filename() == "minified.cpp"; })) result.value().exitCode = 1;
      if (fault == "stdout" && pathFromUtf8(spec.argv.front()).filename() == "pruned.exe" && result.ok())
        result.value().stdoutText = "different output";
      return result;
    }));
  }
};
}
TEST(pipeline_sample_selection_file_directory_order_repetition_and_errors) {
  test::TempDirectory temp;
  std::filesystem::create_directories(temp.path / "samples" / "nested");
  temp.write("one.txt",""); temp.write("samples/z.in",""); temp.write("samples/a.in","");
  temp.write("samples/ignored.txt",""); temp.write("samples/nested/ignored.in","");
  auto samples = take(collectRunSamples({"one.txt","samples","one.txt"},temp.path));
  CHECK_EQ(samples.size(),4u);
  CHECK_EQ(samples[0].name,"one.txt");
  CHECK_EQ(samples[1].name,pathToUtf8(std::filesystem::path("samples") / "a.in"));
  CHECK_EQ(samples[2].input,temp.path / "samples" / "z.in");
  CHECK_EQ(samples[3].input,samples[0].input);
  CHECK(collectRunSamples({},temp.path).value().empty());
  CHECK(!collectRunSamples({"missing"},temp.path).ok());
  std::filesystem::create_directory(temp.path / "empty");
  CHECK(!collectRunSamples({"empty"},temp.path).ok());
  CHECK(!collectRunSamples({"samples/nested/ignored.in","empty"},temp.path).ok());
}
TEST(pipeline_runtime_raw_failures_keep_work_and_flat_difference_falls_back) {
  for (const auto& fault : {"raw-stdout","raw-exit","raw-timeout","raw-cap","flat-stdout"}) {
    Fixture f("int main(){return 7;}\n"); f.options.noPrune = true;
    auto r = f.run(fault);
    if (std::string_view(fault) == "flat-stdout") {
      CHECK_EQ(r.exitCode,0); CHECK_EQ(r.artifact,"raw"); CHECK_EQ(r.runtime,"compared 1 samples");
      CHECK_EQ(r.bytes,f.built.at("raw"));
    } else {
      CHECK_EQ(r.exitCode,1); CHECK(r.bytes.empty()); CHECK(r.keptWork.has_value());
      CHECK(std::filesystem::exists(*r.keptWork / "raw.cpp"));
      CHECK(r.report.find("raw failed:") != r.report.npos);
      std::filesystem::remove_all(*r.keptWork);
    }
    if (std::string_view(fault).ends_with("stdout") || std::string_view(fault) == "raw-exit")
      CHECK(r.diagnostic.find("differs on sample empty sample (distill bug or nondeterministic program)") != r.diagnostic.npos);
  }
}
TEST(pipeline_runtime_stderr_ignored_progress_and_shared_run_limits) {
  Fixture f("int main(){return 7;}\n"); f.options.noPrune = true;
  std::vector<std::string> progress;
  f.options.progress = [&](std::string_view s) { progress.emplace_back(s); };
  f.options.samples.push_back(f.options.samples.front());
  auto r = f.run("stderr"); CHECK_EQ(r.artifact,"flat"); CHECK_EQ(r.runtime,"compared 2 samples");
  CHECK_EQ(std::count_if(progress.begin(),progress.end(),[](const auto& s) { return s.ends_with("sample empty sample matches"); }),4);
  std::size_t runs = 0;
  std::map<std::string,std::chrono::milliseconds> remaining;
  for (const auto& record : r.processes) if (record.spec.argv.size() == 1) {
    ++runs; CHECK(record.spec.timeout.count() > 0); CHECK(record.spec.timeout <= f.options.stepTimeout);
    if (remaining.contains(record.step)) CHECK(record.spec.timeout <= remaining.at(record.step));
    remaining[record.step] = record.spec.timeout;
    CHECK(record.spec.maxOutputBytes <= f.options.maxProcessOutput);
  }
  CHECK_EQ(runs,8u);
  r = f.run("cross"); CHECK_EQ(r.artifact,"flat");
  CHECK_EQ(r.runtime,"skipped: can't run target binaries here");
  CHECK(std::none_of(r.processes.begin(),r.processes.end(),[](const auto& p) { return p.spec.argv.size() == 1; }));
}
TEST(pipeline_pruned_core_exact_checked_bytes_and_samples) {
  Fixture f("int unused(){return 7;}\nint main(){return 0;}\n");
  const auto r = f.run(); CHECK_EQ(r.exitCode,0); CHECK_EQ(r.artifact,"pruned");
  CHECK_EQ(r.runtime,"compared 1 samples"); CHECK_EQ(r.bytes,f.built.at("pruned"));
  CHECK(r.bytes.find("unused") == r.bytes.npos); CHECK(r.assumptions.contains("A6")); CHECK(r.assumptions.contains("A7")); CHECK(r.assumptions.contains("A11"));
  CHECK(r.report.find("assumptions: A11 instantiation-effects A14 unobserved-guard-macros A6 A7 D3f unspelled-functions\n") != r.report.npos);
  CHECK(r.report.find("parse configuration:") != r.report.npos);
  CHECK(r.report.find("primary structural bytes") != r.report.npos);
  // Default (R4-G): markers always, and no line-marker report lines.
  CHECK(r.bytes.find("#line ") != r.bytes.npos); CHECK(r.lineMarkers.empty()); CHECK(r.compactWhitespace.empty());
  CHECK(r.diagnostic.find("line markers") == r.diagnostic.npos); CHECK(!f.built.contains("unmarked"));
}
TEST(pipeline_prunes_without_instantiation_effects_assumption) {
  Fixture f("int unused(){return 7;}\nint main(){return 0;}\n");
  f.options.assumeInstantiationEffects = false;
  const auto r = f.run(); CHECK_EQ(r.exitCode,0); CHECK_EQ(r.artifact,"pruned");
  CHECK(!r.pruningModel->assumeInstantiationEffects); CHECK(!r.assumptions.contains("A11"));
  CHECK(r.bytes.find("unused") == r.bytes.npos); CHECK(r.assumptions.contains("A7"));
  CHECK(r.report.find("policy: D3e, without instantiation-effects") != r.report.npos);
  CHECK(r.report.find("--no-assume=instantiation-effects") != r.report.npos);
  CHECK(r.report.find("pruning assumption: A11") == r.report.npos);
  f.options.noPrune = true;
  const auto flat = f.run(); CHECK_EQ(flat.artifact,"flat"); CHECK(!flat.pruningModel);
  CHECK(flat.report.find("pruning assumption disabled: A11 instantiation-effects (--no-assume=instantiation-effects)") != flat.report.npos);
}
TEST(pipeline_no_line_markers_omitted_after_checks) {
  for (bool strip : {false,true}) {
    Fixture f("int unused(){return 7;}\n/* c */ int main(){return 0;}\n");
    f.options.noLineMarkers = true; f.options.stripComments = strip;
    const auto r = f.run(); CHECK_EQ(r.exitCode,0); CHECK_EQ(r.artifact,"pruned"); CHECK_EQ(r.lineMarkers,"omitted");
    CHECK(f.built.at("pruned").find("#line ") != std::string::npos);
    CHECK_EQ(r.bytes,f.built.at("unmarked")); CHECK(r.bytes.find("#line") == r.bytes.npos);
    CHECK(r.bytes.find("int unused") == r.bytes.npos); CHECK(r.bytes.find("int main(){return 0;}") != r.bytes.npos);
    CHECK_EQ(r.runtime,"compared 1 samples");
    CHECK(r.diagnostic.find("line markers omitted: no user code observes presumed source positions\n") != r.diagnostic.npos);
    CHECK(r.report.find("pruned without line markers: passed\n") != r.report.npos);
    CHECK(r.resultLine.starts_with("distill: wrote pruned ("));
  }
}
TEST(pipeline_no_line_markers_kept_for_position_uses) {
  struct Case { const char* fault; const char* source; const char* why; };
  for (const auto& c : {Case{"position","int unused(){return 7;}\nint main(){return __LINE__ - 2;}\n","__LINE__ expanded at main.cpp:2"},
                        Case{"expansion","int unused(){return 7;}\nint main(){return __LINE__ - 2;}\n","__LINE__ expanded at main.cpp:2"},
                        Case{"default","int unused(){return 7;}\nint where(int l=0){return l;}\nint main(){return where();}\n",
                             "call-site default argument with a source position at main.cpp:3"}}) {
    Fixture f(c.source); f.options.noLineMarkers = true;
    const auto r = f.run(c.fault); CHECK_EQ(r.exitCode,0); CHECK_EQ(r.artifact,"pruned");
    CHECK_EQ(r.bytes,f.built.at("pruned")); CHECK(r.bytes.find("#line ") != r.bytes.npos);
    CHECK(!f.built.contains("unmarked")); CHECK_EQ(r.lineMarkers,std::string("kept: ") + c.why);
    CHECK(r.diagnostic.find(std::string("line markers kept: ") + c.why + "\n") != r.diagnostic.npos);
    CHECK(r.report.find(std::string("line markers kept: ") + c.why + "\n") != r.report.npos);
  }
}
TEST(pipeline_no_line_markers_kept_without_parse_or_pruned) {
  {
    Fixture f("int main(){return 0;}\n"); f.options.noLineMarkers = true; f.options.noPrune = true;
    const auto r = f.run(); CHECK_EQ(r.artifact,"flat"); CHECK_EQ(r.bytes,f.built.at("flat"));
    CHECK_EQ(r.lineMarkers,"kept: no parse"); CHECK(r.diagnostic.find("line markers kept: no parse\n") != r.diagnostic.npos);
  }
  {
    Fixture f("int unused(){return 7;}\nint main(){return 0;}\n"); f.options.noLineMarkers = true;
    const auto r = f.run("disabled"); CHECK_EQ(r.artifact,"flat"); CHECK_EQ(r.bytes,f.built.at("flat"));
    CHECK_EQ(r.lineMarkers,"kept: no checked pruned artifact");
  }
  {
    // A failed marker-free check keeps the checked marked pruned bytes.
    Fixture f("int unused(){return 7;}\nint main(){return 0;}\n"); f.options.noLineMarkers = true;
    const auto r = f.run("unmarked-stdout"); CHECK_EQ(r.exitCode,0); CHECK_EQ(r.artifact,"pruned");
    CHECK_EQ(r.bytes,f.built.at("pruned")); CHECK(f.built.contains("unmarked"));
    CHECK(r.lineMarkers.starts_with("kept: marker-free check failed: differs on sample empty sample"));
    CHECK(r.report.find("pruned without line markers: differs on sample") != r.report.npos);
  }
}
TEST(pipeline_pruned_without_deletions_still_reports_a7) {
  Fixture f("int main(){return 0;}\n");
  const auto r = f.run(); CHECK_EQ(r.artifact,"pruned");
  CHECK(r.assumptions.contains("A7")); CHECK(r.assumptions.contains("A11"));
  CHECK(r.report.find("assumptions: A11 instantiation-effects A14 unobserved-guard-macros A7 D3f unspelled-functions\n") != r.report.npos);
}
TEST(pipeline_pruned_reproduced_diagnostic_separate_from_result_line) {
  Fixture f("int unused(){return 7;}\nint main(){return 0;}\n");
  auto profile = take(testing::captureProfile(f.gcc,f.temp.path));
  f.options.profile = &profile;
  const auto r = f.run(); CHECK_EQ(r.exitCode,0); CHECK_EQ(r.artifact,"pruned");
  CHECK(r.diagnostic.find("check compiler unavailable: distill-missing-check-719239\n") != r.diagnostic.npos);
  CHECK_EQ(r.verification,profile.reproduced ? "target" : "local");
  CHECK_EQ(r.resultLine,"distill: wrote pruned (" + r.verification + "), runtime compared 1 samples");
}
TEST(pipeline_identity_satisfaction_and_header_violation) {
  Fixture f("#include <cstdint>\n#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))\n"
            "#include <intrin.h>\n#endif\nint unused(){return 7;}\nint main(){return 0;}\n");
  auto profile = take(testing::captureProfile(f.gcc,f.temp.path));
  if (profile.triple.find("linux") == profile.triple.npos || !profile.reproduced)
    throw test::Skip{"requires reproduced Linux fixture"};
  f.options.profile = &profile;
  auto r = f.run(); CHECK_EQ(r.exitCode,0); CHECK_EQ(r.artifact,"pruned");
  CHECK(!r.resolution.unknownInUserCode);
  CHECK(r.bytes.find("intrin.h") == r.bytes.npos); CHECK(r.bytes.find("int unused") == r.bytes.npos);
  CHECK(r.assumptions.contains("A2"));
  // Simulate a replacement standard header violating A2. Both model reruns
  // agree on initial absence; the actual preprocessing comparison must reject.
  f.options.noPrune = true;
  std::filesystem::create_directory(f.temp.path / "system");
  f.temp.write("system/cstdint", "#define _MSC_VER 1\n");
  f.temp.write("main.cpp", "#include <cstdint>\n#ifdef _MSC_VER\nint chosen=1;\n#else\nint chosen=0;\n#endif\nint main(){return chosen;}\n");
  f.command = take(parseCompileCommand({pathToUtf8(f.gcc),"-std=gnu++20","-isystem","system","main.cpp"},f.temp.path));
  r = f.run(); CHECK_EQ(r.exitCode,0); CHECK_EQ(r.artifact,"raw");
  CHECK(r.diagnostic.find("preprocess difference") != r.diagnostic.npos);
}
TEST(pipeline_approximate_shipped_profile_identity_and_uint64_constant) {
  Fixture f("#include <cstdint>\n#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))\n"
            "#include <intrin.h>\n#endif\nint unused(){return 7;}\nint main(){return UINT64_C(0);}\n");
  const auto local = take(probeCompiler(f.gcc, f.command, {true, {}, f.temp.path}));
  if (local.triple.find("linux") == local.triple.npos || local.macros.at("__SIZEOF_LONG__").replacement != "8")
    throw test::Skip{"requires LP64 Linux GCC for the shipped QOJ constant definitions"};
  auto profile = take(lookupProfile("qoj")); CHECK(!profile.reproduced);
  f.options.profile = &profile;
  auto diagnose = [](const PipelineResult& r, int before) {
    if (test::failures == before) return;
    std::cerr << r.diagnostic;
    if (r.pruningModel) std::cerr << "pruning model disabled reasons: " << test::show(r.pruningModel->disabledReasons) << '\n';
    if (r.retention) std::cerr << "pruning retention disabled reasons: " << test::show(r.retention->disabledReasons) << '\n';
  };
  PipelineResult r;
  for (const bool nativeCheck : {false, true}) {
    if (nativeCheck) {
      // Also exercise the compatible check path on LP64 aarch64 hosts, where
      // the shipped x86_64 triple otherwise selects the local fallback.
      profile.triple = local.triple;
      profile.check = {pathToUtf8(f.gcc)};
    }
    const auto before = test::failures;
    r = f.run("uint64");
    CHECK_EQ(r.exitCode, 0); CHECK_EQ(r.artifact, "pruned");
    CHECK(!r.resolution.unknownInUserCode); CHECK(f.constantExpansionObserved);
    CHECK(r.bytes.find("intrin.h") == r.bytes.npos); CHECK(r.bytes.find("int unused") == r.bytes.npos);
    CHECK(r.bytes.find("UINT64_C(0)") != r.bytes.npos);
    CHECK_EQ(r.runtime, "compared 1 samples");
    if (nativeCheck) CHECK(std::any_of(r.checks.begin(), r.checks.end(), [](const auto& check) {
      return check.artifact == "flat" && check.status == "passed" && check.configuration &&
          check.configuration->kind == "check" && std::any_of(check.configuration->command.args.begin(),
              check.configuration->command.args.end(), [](const auto& a) { return a.spelling == std::vector<std::string>{"-lm"}; });
    }));
    diagnose(r, before);
  }
  profile.snapshotFacts.at("__UINT64_C").definition->replacement = "c ## ULL";
  const auto before = test::failures;
  r = f.run("uint64");
  CHECK_EQ(r.exitCode, 0); CHECK_EQ(r.artifact, "flat");
  CHECK(r.diagnostic.find("predefined macro __UINT64_C: unknown judge fact") != r.diagnostic.npos);
  diagnose(r, before);
}
TEST(pipeline_approximate_shipped_profile_x86_identity) {
  Fixture f("#include <cstdint>\n"
            "#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)\n"
            "int unused(){return 7;}\nint main(){return 0;}\n"
            "#else\n#error expected x86 target\n#endif\n");
  const auto local = take(probeCompiler(f.gcc, f.command, {true, {}, f.temp.path}));
  if (local.triple.find("linux") == local.triple.npos || !local.macros.contains("__x86_64__"))
    throw test::Skip{"requires x86_64 Linux GCC for the shipped QOJ architecture"};
  auto profile = take(lookupProfile("qoj")); CHECK(!profile.reproduced);
  f.options.profile = &profile;
  const auto before = test::failures;
  const auto r = f.run();
  CHECK_EQ(r.exitCode, 0); CHECK_EQ(r.artifact, "pruned");
  CHECK(!r.resolution.unknownInUserCode);
  CHECK_EQ(r.resolution.decisions.at(0).decision, Decision::trueValue);
  CHECK(r.bytes.find("#if") == r.bytes.npos); CHECK(r.bytes.find("#error") == r.bytes.npos);
  CHECK(r.bytes.find("int unused") == r.bytes.npos);
  CHECK_EQ(r.runtime, "compared 1 samples"); CHECK(r.assumptions.contains("A2"));
  if (test::failures != before) std::cerr << r.diagnostic;
}
TEST(pipeline_pruned_core_disabled_and_render_fallback) {
  for (const auto& fault : {"disabled","render","stdout"}) {
    Fixture f("int unused(){return 7;}\nint main(){return 0;}\n");
    f.options.stripComments = true;
    auto r = f.run(fault); CHECK_EQ(r.exitCode,0); CHECK_EQ(r.artifact,"flat");
    CHECK_EQ(r.bytes,f.built.at("flat")); CHECK_EQ(r.runtime,"compared 1 samples");
    CHECK(!r.assumptions.contains("A6")); CHECK(!r.assumptions.contains("A7")); CHECK(!r.assumptions.contains("A11"));
    if (std::string_view(fault) == "stdout") CHECK(r.diagnostic.find("differs on sample empty sample") != r.diagnostic.npos);
  }
}
TEST(pipeline_pruned_core_strip_keep_through_resolution) {
  Fixture f("#if 1\n// distill: keep\nint preserve(){return 7;}\n#endif\nint unused(){return 9;}\nint main(){return 0;}\n");
  f.options.stripComments = true;
  auto r = f.run(); CHECK_EQ(r.artifact,"pruned"); CHECK(f.strippedKeepObserved);
  CHECK(r.bytes.find("int preserve") != r.bytes.npos); CHECK(r.bytes.find("distill: keep") == r.bytes.npos);
  CHECK(r.bytes.find("int unused") == r.bytes.npos);
}
TEST(pipeline_written_artifacts_only_name_original_sources) {
  for (bool strip : {false,true}) {
    // Root/header once guards, repeated and recursive includes, rewritten system
    // includes, conditional resyncs and stripped multiline comments all produce
    // generated lines. Exercise the actual files passed to the compiler.
    Fixture f("#pragma once\n#include <cstdio>\n#if 1\n#include \"lib/ds.h\"\n#endif\n"
              "#include \"lib/ds.h\"\nint main(){Fenwick f(10); return f.n != 10;}\n");
    std::filesystem::create_directory(f.temp.path / "lib");
    f.temp.write("lib/ds.h", "#pragma once\n#include \"ds.h\"\n#include \"vector\"\n"
                 "/* header\ncomment */ struct Fenwick {int n; explicit Fenwick(int v):n(v){}};\n"
                 "struct DSU {};\nint unused(){return 9;}\n");
    f.options.stripComments = strip;
    auto result = f.run();
    CHECK_EQ(result.exitCode,0); CHECK_EQ(result.artifact,"pruned");
    CHECK_EQ(result.bytes,f.built.at("pruned"));
    CHECK(result.bytes.find("int unused") == result.bytes.npos);
    for (const auto* artifact : {"raw","flat","pruned"}) {
      const auto& bytes = f.built.at(artifact);
      auto scan = take(scanDirectives(bytes));
      std::size_t markers = 0, shortMarkers = 0;
      PresumedFile presumed; // Short markers inherit the file of their segment.
      for (const auto& l : scan.lines) if (l.directive && l.directive->name == "line") {
        auto text = std::string_view(bytes).substr(l.begin,l.end-l.begin);
        if (text.ends_with('\n')) text.remove_suffix(1);
        SourceLocation at;
        CHECK(take(presumed.parse(text,at))); ++markers;
        shortMarkers += text.find('"') == text.npos;
        CHECK(at.file == "main.cpp" || at.file == "lib/ds.h");
      } else if (l.directive) presumed.directive(l.directive->name);
      CHECK(markers > 0);
      if (std::string_view(artifact) == "pruned") CHECK(shortMarkers > 0);
      CHECK((bytes.find("#define DISTILL_ONCE_") != bytes.npos) == (std::string_view(artifact) != "pruned"));
      if (std::string_view(artifact) == "raw") CHECK(bytes.find("#ifndef DISTILL_ONCE_") != bytes.npos);
      else CHECK(bytes.find("#ifndef DISTILL_ONCE_") == bytes.npos);
    }
  }
}
TEST(pipeline_pruned_core_keep_and_unknown_name) {
  for (const auto& name : {"unused","missing"}) {
    Fixture f("int unused(){return 7;}\nint main(){return 0;}\n"); f.options.keep = {name};
    auto r = f.run();
    if (std::string_view(name) == "unused") { CHECK_EQ(r.artifact,"pruned"); CHECK(r.bytes.find("int unused") != r.bytes.npos); }
    else { CHECK_EQ(r.exitCode,1); CHECK(r.bytes.empty()); CHECK(r.diagnostic.find("--keep missing") != r.diagnostic.npos); }
  }
}
TEST(pipeline_pruned_core_strict_and_bad_build) {
  Fixture f("#define CAT_I(a,b) a##b\n#define CAT(a,b) CAT_I(a,b)\nint chosen(){return 7;}\nint main(){return CAT(cho,sen)();}\n");
  f.options.strict = true;
  const auto r = f.run(); CHECK_EQ(r.exitCode,3); CHECK_EQ(r.artifact,"flat");
  CHECK_EQ(r.bytes,f.built.at("flat")); CHECK(r.diagnostic.find("chosen") != r.diagnostic.npos);
}
TEST(pipeline_pruned_core_forced_bad_removal_caught_by_run) {
  Fixture f("#define CAT_I(a,b) a##b\n#define CAT(a,b) CAT_I(a,b)\nint chosen(int){return 7;}\n"
            "template<class T> int call(T x){if constexpr(requires{CAT(cho,sen)(x);}) return CAT(cho,sen)(x); else return 0;}\n"
            "int main(){return call(1);}\n");
  auto r = f.run(); CHECK_EQ(r.artifact,"flat"); CHECK_EQ(r.bytes,f.built.at("flat"));
  CHECK(r.diagnostic.find("differs on sample empty sample") != r.diagnostic.npos);
  CHECK(f.built.contains("pruned")); CHECK(f.built.at("pruned").find("int chosen") == std::string::npos);
}

TEST(pipeline_preprocess_cap_is_separate_and_overflow_fails_closed) {
  Fixture f("int main(){return 0;}\n"); f.options.noPrune = true;
  auto result = f.run(); CHECK_EQ(result.artifact,"flat");
  std::size_t preprocesses = 0;
  for (const auto& process : result.processes) {
    const auto& spec = process.spec;
    const bool check = (process.step == "raw checks" || process.step == "flat checks") &&
        std::find(spec.argv.begin(),spec.argv.end(),"-E") != spec.argv.end() &&
        std::find(spec.argv.begin(),spec.argv.end(),"-dM") == spec.argv.end() &&
        std::find(spec.argv.begin(),spec.argv.end(),"-v") == spec.argv.end();
    // Startup/special-macro probes also use -E, but write their own source.
    bool source = false;
    for (const auto& arg : spec.argv) {
      auto name = pathFromUtf8(arg).filename();
      source |= name == "main.cpp" || name == "raw.cpp" || name == "flat.cpp";
    }
    if (check && source) { ++preprocesses; CHECK_EQ(spec.maxOutputBytes,64u * 1024 * 1024); }
    else CHECK(spec.maxOutputBytes <= 4u * 1024 * 1024);
  }
  CHECK_EQ(preprocesses,4u);
  f.options.maxPreprocessOutput = 16;
  result = f.run(); CHECK_EQ(result.exitCode,1);
  CHECK(result.diagnostic.find("raw failed: raw checks: process output cap exceeded") != result.diagnostic.npos);
  if (result.keptWork) std::filesystem::remove_all(*result.keptWork);
}
TEST(pipeline_library_fact_reports_a9_and_survives_local_satisfaction) {
  Fixture f("#include <charconv>\n#if defined(__cpp_lib_to_chars)\nint main(){return 0;}\n#else\n#error missing charconv\n#endif\n");
  auto p = lookupProfile("codeforces").value(); f.options.profile = &p; f.options.noPrune = true;
  auto r = f.run();
  CHECK_EQ(r.exitCode, 0); CHECK_EQ(r.artifact, "flat");
  CHECK(!r.resolution.unknownInUserCode);
  CHECK(r.assumptions.contains("A9"));
  CHECK(r.report.find("library facts: " + std::string(embeddedLibraryData().provenance)) != r.report.npos);
  CHECK(r.report.find("selector=gcc-14") != r.report.npos);
  CHECK(r.report.find("__cpp_lib_to_chars: A9 library") != r.report.npos);
  CHECK_EQ(r.resolution.decisions[0].decision, Decision::trueValue);
  CHECK(r.report.find("resolution assumption: A9 (feature-test macro facts only)\n") != r.report.npos);
  f.options.assumeLibraryMacroValues = false;
  r = f.run();
  CHECK_EQ(r.exitCode, 0); CHECK_EQ(r.artifact, "flat");
  CHECK_EQ(r.resolution.decisions[0].decision, Decision::trueValue);
  CHECK(r.assumptions.contains("A9")); CHECK(!r.resolution.libraryMacroValuesUsed);
  CHECK(r.report.find("resolution assumption: A9 (feature-test macro facts only)\n") != r.report.npos);
  CHECK(r.report.find("resolution assumption disabled: A9 library-macro-values (--no-assume=library-macro-values); __cpp_lib_* facts retained\n") != r.report.npos);
}
TEST(pipeline_compact_checked_with_stringizing_and_keep) {
  Fixture f("#define TEXT(x) #x\n"
            "  constexpr auto s = TEXT(alpha\n\n    beta);  \n"
            "  static_assert(s[5] == ' ' && s[6] == 'b');  \n"
            "  constexpr auto raw = u8R\"x(\n  raw \t\n\n)x\";  \n"
            "int unused(){return 7;}\n"
            "// distill: keep\nint preserve(){return 3;}\n"
            "  int main(){return 0;}  \n\n");
  f.options.compactWhitespace = true; f.options.stripComments = true;
  const auto r = f.run();
  CHECK_EQ(r.exitCode,0); CHECK_EQ(r.artifact,"pruned");
  CHECK_EQ(r.lineMarkers,"omitted"); CHECK_EQ(r.compactWhitespace,"compacted");
  CHECK_EQ(r.bytes,f.built.at("compact")); CHECK(f.strippedKeepObserved);
  CHECK(r.bytes.find("int preserve") != r.bytes.npos); CHECK(r.bytes.find("int unused") == r.bytes.npos);
  CHECK(r.bytes.find("u8R\"x(\n  raw \t\n\n)x\"") != r.bytes.npos);
  CHECK(r.bytes.size() < f.built.at("unmarked").size());
  CHECK(r.report.find("pruned with compact whitespace: passed\n") != r.report.npos);
  CHECK(std::any_of(r.processes.begin(),r.processes.end(),[](const auto& p) {
    return p.step == "compact checks" && std::find(p.spec.argv.begin(),p.spec.argv.end(),"-E") != p.spec.argv.end();
  }));
}
TEST(pipeline_compact_refused_or_failed_keeps_checked_bytes) {
  for (const auto* fault : {"position","disabled","no-parse","unmarked-stdout","compact-tokens","compact-build","compact-stdout"}) {
    Fixture f(std::string_view(fault) == "position" ? "int unused(){return 7;}\n  int main(){return __LINE__ > 0 ? 0 : 1;}  \n\n" :
              "int unused(){return 7;}\n  int main(){return 0;}  \n\n");
    f.options.compactWhitespace = true;
    f.options.noPrune = std::string_view(fault) == "no-parse";
    const auto r = f.run(fault); CHECK_EQ(r.exitCode,0);
    const std::string_view why = fault;
    const auto fallback = why == "disabled" || why == "no-parse" ? "flat" :
                          why == "position" || why == "unmarked-stdout" ? "pruned" : "unmarked";
    CHECK_EQ(r.bytes,f.built.at(fallback)); CHECK(r.compactWhitespace.starts_with("kept: "));
    CHECK(r.report.find("whitespace kept: ") != r.report.npos);
    if (why.starts_with("compact-")) {
      CHECK(r.compactWhitespace.starts_with("kept: compact check failed: "));
      CHECK_EQ(r.lineMarkers,"omitted");
    } else CHECK(!f.built.contains("compact"));
  }
}

TEST(pipeline_pp_trimming_preserves_raw_flat_and_position_gate) {
  Fixture f("#include <vector>\n#include \"once.h\"\n#include <vector>\nint main(){return __LINE__ != 4;}\n");
  f.temp.write("once.h","#pragma once\n");
  f.options.noLineMarkers = true;
  const auto trimmed = f.run("position");
  CHECK_EQ(trimmed.artifact,"pruned"); CHECK(trimmed.lineMarkers.starts_with("kept:"));
  CHECK_EQ(trimmed.retention->trimming.includeLines,1u);
  CHECK_EQ(trimmed.retention->trimming.guardLines,1u);
  CHECK(trimmed.bytes.find("#define DISTILL_ONCE_") == trimmed.bytes.npos);
  const auto raw = f.built.at("raw"), flat = f.built.at("flat");
  f.options.assumeIdempotentStandardHeaders = false;
  f.options.assumeUnobservedGuardMacros = false;
  const auto opted = f.run("position");
  CHECK_EQ(opted.artifact,"pruned"); CHECK_EQ(opted.runtime,"compared 1 samples");
  CHECK_EQ(opted.retention->trimming.includeLines,0u);
  CHECK_EQ(opted.retention->trimming.guardLines,0u);
  CHECK(opted.bytes.find("#define DISTILL_ONCE_") != opted.bytes.npos);
  CHECK_EQ(raw,f.built.at("raw")); CHECK_EQ(flat,f.built.at("flat"));
  CHECK(!opted.assumptions.contains("A14"));
}

TEST(pipeline_minify_implied_flags_report_and_checked_fallbacks) {
  const std::string source = "// strip this comment\nint main(){int longLocal=5;return longLocal-5;}\n";
  for (const auto& fault : {"", "minify-library", "minify-build", "minify-stdout"}) {
    Fixture f(source); f.options.minify = true;
    const auto r = f.run(fault);
    CHECK_EQ(r.exitCode,0); CHECK_EQ(r.artifact,"pruned");
    CHECK_EQ(r.lineMarkers,"omitted"); CHECK_EQ(r.compactWhitespace,"compacted");
    CHECK(r.bytes.find("strip this comment") == r.bytes.npos);
    if (std::string_view(fault).empty()) {
      CHECK(r.minify.starts_with("renamed=")); CHECK(r.assumptions.contains("A15"));
      CHECK(r.bytes.find("longLocal") == r.bytes.npos); CHECK_EQ(r.bytes,f.built.at("minified"));
      CHECK(r.report.find("A15 library-spelled-names") != r.report.npos);
    } else {
      CHECK(r.minify.starts_with("kept: ")); CHECK(!r.assumptions.contains("A15"));
      CHECK_EQ(r.bytes,f.built.at("compact"));
    }
  }
  Fixture f(source); f.options.minify = true; f.options.assumeLibrarySpelledNames = false;
  const auto r = f.run(); CHECK_EQ(r.bytes,f.built.at("minified"));
  CHECK(r.bytes.find("longLocal") != r.bytes.npos); CHECK(!r.assumptions.contains("A15"));
  CHECK(r.minify.find("A15 library-spelled-names disabled") != r.minify.npos);
}
