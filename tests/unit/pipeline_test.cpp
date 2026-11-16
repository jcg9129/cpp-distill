// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.h"
#include "core/pipeline/pipeline.h"
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
        if (fault == "render") {
          auto& range = m.families.front().ranges.front(); ++range.begin; // Split 'int'.
        }
        ProcessResult r; r.exitCode = 0; r.stdoutText = take(serializePruningModel(m)); return r;
      }
      for (const auto& arg : spec.argv) {
        const auto p = pathFromUtf8(arg);
        if (p.filename() == "raw.cpp" || p.filename() == "flat.cpp" || p.filename() == "pruned.cpp")
          built[pathToUtf8(p.stem())] = test::read(p);
      }
      auto result = runProcess(spec);
      const auto program = pathFromUtf8(spec.argv.front()).filename();
      if (result.ok() && fault == "cross" && std::find(spec.argv.begin(),spec.argv.end(),"-dumpmachine") != spec.argv.end())
        result.value().stdoutText = "unrunnable-distill-test-target\n";
      if (result.ok() && spec.argv.size() == 1) {
        if (fault == "stderr") result.value().stderrText = pathToUtf8(program);
        if ((fault == "raw-stdout" && program == "raw.exe") || (fault == "flat-stdout" && program == "flat.exe"))
          result.value().stdoutText = "different sample output";
        if (fault == "raw-exit" && program == "raw.exe") ++result.value().exitCode;
        if (fault == "raw-timeout" && program == "raw.exe") result.value().timedOut = true;
        if (fault == "raw-cap" && program == "raw.exe") result.value().stdoutTruncated = true;
      }
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
  CHECK(r.bytes.find("unused") == r.bytes.npos); CHECK(r.assumptions.contains("A6"));
  CHECK(r.report.find("parse configuration:") != r.report.npos);
  CHECK(r.report.find("primary structural bytes") != r.report.npos);
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
TEST(pipeline_pruned_core_disabled_and_render_fallback) {
  for (const auto& fault : {"disabled","render","stdout"}) {
    Fixture f("int unused(){return 7;}\nint main(){return 0;}\n");
    f.options.stripComments = true;
    auto r = f.run(fault); CHECK_EQ(r.exitCode,0); CHECK_EQ(r.artifact,"flat");
    CHECK_EQ(r.bytes,f.built.at("flat")); CHECK_EQ(r.runtime,"compared 1 samples");
    CHECK(!r.assumptions.contains("A6"));
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
