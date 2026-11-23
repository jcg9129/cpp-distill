// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.h"
#include "evaluate.h"
#include "core/lex/scan.h"
#include "core/report/report.h"
#include <iostream>

using namespace distill;
template<class T> T take(Result<T> result) {
  if (!result.ok()) throw std::runtime_error(result.error().message);
  return std::move(result.value());
}
const std::string function = R"(constexpr double rounded_binary64(double value) {
#if FLT_EVAL_METHOD != 0
  if consteval { return value; }
  else { const volatile double stored = value; return stored; }
#else
  return value;
#endif
}
)";
int main() try {
  test::TempDirectory temp;
  const auto gcc = testing::testGcc();
  if (!gcc) {
    std::cout << "library macro regression skipped: GCC unavailable\n";
    return 77;
  }
  auto evaluator = testing::testEvaluator(temp.path);
  if (!evaluator) throw std::runtime_error("condition evaluator unavailable");
  auto profile = take(lookupProfile("cses"));
  CHECK(!profile.reproduced);
  ResolveOptions options; options.profile = &profile;
  // Arithmetic is evaluated using an explicit model; the macro value still
  // comes exclusively from the synthetic profile's library capture.
  auto command = take(parseCompileCommand({pathToUtf8(*gcc), "main.cpp", "-std=c++23", "-fsigned-char"}, temp.path));
  const auto observed = take(probeCompiler(*gcc, command, {true, {}, temp.path}));
  Profile local; local.reproduced = true;
  for (const auto& [name, macro] : observed.macros)
    local.snapshotFacts[name] = {MacroState::known, macro, {FactOrigin::compilerBuiltin}};
  options.requirements.integers = profileRequirements(local).integers;
  CHECK(options.requirements.integers.has_value());
  const auto run = [&](std::string prefix, std::string suffix = {}, std::vector<std::string> flags = {}, bool own = true) {
    InlineResult raw; raw.text = prefix + function + suffix;
    for (std::size_t i = 0, line = 1; i < raw.text.size(); ++i)
      if (i == 0 || raw.text[i - 1] == '\n') raw.lineMap.push_back(SourceLocation{pathToUtf8(command.source), line++});
    const auto scan = take(scanDirectives(raw.text));
    for (const auto& line : scan.lines) if (line.directive && line.directive->name == "include") {
      const auto& operand = line.directive->tokens.front().text;
      raw.systemIncludes.push_back({{pathToUtf8(command.source), line.tokens.front().line},
          operand.substr(1, operand.size() - 2), true, false, true, true, {}, own});
    }
    std::vector<std::string> argv{pathToUtf8(*gcc), "main.cpp", "-std=c++23", "-fsigned-char"};
    argv.insert(argv.end(), flags.begin(), flags.end());
    const auto invocation = take(parseCompileCommand(argv, temp.path));
    return take(resolveConditions(raw, invocation, *evaluator, options));
  };
  const auto unknown = [&](std::string prefix, std::vector<std::string> flags = {}) {
    const auto result = run(std::move(prefix), {}, std::move(flags));
    CHECK(result.decisions.at(result.decisions.size() - 3).decision == Decision::unknown);
    CHECK(result.text.find("#if FLT_EVAL_METHOD != 0") != std::string::npos);
  };
  auto result = run("#include <cfloat>\n");
  CHECK(result.decisions.at(0).decision == Decision::falseValue);
  CHECK(result.text.find("volatile") == std::string::npos);
  CHECK(result.text.find("#if") == std::string::npos);
  CHECK(result.decisions[0].external.at(0).name == "FLT_EVAL_METHOD");
  CHECK(result.decisions[0].external[0].fact.provenance == std::vector{FactOrigin::library});
  CHECK(result.libraryProvenance.size() == 1);
  CHECK(result.libraryProvenance.begin()->find("generator=5 schema=2") != std::string::npos);
  PipelineResult pipeline; pipeline.resolution = result; pipeline.assumptions.insert("A9");
  const auto report = renderReport(pipeline, command, &profile);
  CHECK(report.find("FLT_EVAL_METHOD: A9 library") != std::string::npos);
  CHECK(report.find("selector=gcc-13") != std::string::npos);
  const auto scope = embeddedLibraryData().selector(profile);
  CHECK(scope.starts_with("gcc-13@"));
  CHECK(embeddedLibraryData().featureAgreementSelector(profile) == "gcc-13");
  CHECK(embeddedLibraryData().find(scope, "cmath") == embeddedLibraryData().find("gcc-13", "cmath"));
  const auto* majorFloat = embeddedLibraryData().find("gcc-13", "cfloat");
  CHECK(majorFloat && embeddedLibraryData().state(*majorFloat, "FLT_EVAL_METHOD") == 1);
  const auto capturedEval = profile.snapshotFacts.at("__FLT_EVAL_METHOD__");
  profile.snapshotFacts.at("__FLT_EVAL_METHOD__").definition->replacement = "2";
  unknown("#include <cfloat>\n");
  profile.snapshotFacts.at("__FLT_EVAL_METHOD__") = capturedEval;
  profile.snapshotFacts.erase("__FLT_EVAL_METHOD__");
  unknown("#include <cfloat>\n");
  profile.snapshotFacts["__FLT_EVAL_METHOD__"] = capturedEval;
  profile.snapshotFacts["__STDC_WANT_IEC_60559_TYPES_EXT__"] = {
      MacroState::known, Macro{false, {}, false, "1"}, {FactOrigin::compilerBuiltin}};
  unknown("#include <cfloat>\n");
  profile.snapshotFacts.erase("__STDC_WANT_IEC_60559_TYPES_EXT__");
  profile.flags.push_back("-march=native");
  unknown("#include <cfloat>\n");
  profile.flags.pop_back();
  // A nonzero capture retains the volatile store, irrespective of worker predefines.
  result = run("#include <float.h>\n");
  CHECK(result.decisions.at(0).decision == Decision::trueValue);
  CHECK(result.text.find("volatile") != std::string::npos);
  CHECK(result.text.find("#if") == std::string::npos);
  result = run("#include <cfloat>\n#include <cmath>\n#include <cfloat>\n");
  CHECK(result.decisions.at(0).decision == Decision::falseValue);
  options.libraryData = nullptr;
  unknown("#include <cfloat>\n");
  options.libraryData = &embeddedLibraryData();
  for (const auto change : {"#define __X 1\n", "#undef __X\n", "#define FLT_EVAL_METHOD 2\n",
                            "#undef FLT_EVAL_METHOD\n", "#define NDEBUG 1\n"})
    unknown(std::string(change) + "#include <cfloat>\n");
  for (const auto change : {"#define __FLT_EVAL_METHOD__ 2\n", "#undef __FLT_EVAL_METHOD__\n",
                            "#define __STDC_WANT_IEC_60559_TYPES_EXT__ 1\n",
                            "#define __STDC_WANT_IEC_60559_EXT__ 1\n",
                            "#define __STDC_VERSION__ 202311L\n", "#undef __X\n"})
    unknown("#include <cfloat>\n" + std::string(change));
  result = run("#include <cfloat>\n#define local 1\n");
  CHECK(result.decisions.at(0).decision == Decision::falseValue);
  unknown("#include <version>\n"); // Cross-build value disagreement.
  unknown("#include <cfloat>\n#include <float.h>\n#include <cfloat>\n");
  unknown("#include <cfloat>\n#include <version>\n#include <cfloat>\n");
  unknown("#if __unknown\n#define __X 1\n#endif\n#include <cfloat>\n");
  unknown("#include <cfloat>\n#pragma GCC target(\"x87\")\n");
  unknown("#include <cfloat>\n", {"-D__X=1"});
  unknown("#include <cfloat>\n", {"-UFLT_EVAL_METHOD"});
  result = run("#include <cfloat>\n", {}, {}, false);
  CHECK(result.decisions.at(0).decision == Decision::unknown);
  const auto capturedTarget = profile.triple;
  profile.triple = "aarch64-linux-gnu";
  unknown("#include <cfloat>\n");
  CHECK(embeddedLibraryData().selector(profile).empty());
  profile.triple = capturedTarget;
  // No profile data means no fallback to the worker or another profile's argv.
  for (const auto& flags : {std::vector<std::string>{"-O2", "-m32"},
                          std::vector<std::string>{"-O2", "-mfpmath=387"},
                          std::vector<std::string>{}}) {
    profile.flags = flags;
    unknown("#include <cfloat>\n");
    CHECK(embeddedLibraryData().selector(profile).empty());
  }
  // Reverse direction also fails: no flags cannot borrow a flagged capture.
  constexpr std::string_view x87[]{"-O2", "-mfpmath=387"};
  const LibraryBuild builds[]{{"", "13", "13.3.0", "c++23", "x86_64-linux-gnu", "", x87}};
  auto data = embeddedLibraryData(); data.builds = builds;
  CHECK(data.selector(profile).empty());
  profile.flags = {"-O2", "-mfpmath=387"};
  CHECK(data.selector(profile) == "gcc-13");
  profile.flags = {"-mfpmath=387", "-O2"};
  CHECK(data.selector(profile).empty());
  if (test::failures) return 1;
  std::cout << "library macro capture/resolver regression passed\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n'; return 1;
}
