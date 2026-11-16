// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.h"
#include "core/pipeline/pipeline.h"
#include "evaluate.h"
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
      if (fault == "stdout" && pathFromUtf8(spec.argv.front()).filename() == "pruned.exe" && result.ok())
        result.value().stdoutText = "different output";
      return result;
    }));
  }
};
}
TEST(pipeline_pruned_core_exact_checked_bytes_and_samples) {
  Fixture f("int unused(){return 7;}\nint main(){return 0;}\n");
  const auto r = f.run(); CHECK_EQ(r.exitCode,0); CHECK_EQ(r.artifact,"pruned");
  CHECK_EQ(r.runtime,"compared 1 samples"); CHECK_EQ(r.bytes,f.built.at("pruned"));
  CHECK(r.bytes.find("unused") == r.bytes.npos); CHECK(r.assumptions.contains("A6"));
  CHECK(r.report.find("parse configuration:") != r.report.npos);
  CHECK(r.report.find("primary structural bytes") != r.report.npos);
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
