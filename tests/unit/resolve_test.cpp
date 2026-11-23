// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.h"
#include "core/resolve/resolve.h"
#include "core/lex/scan.h"
#include "core/report/report.h"
#include "evaluate.h"
#include <algorithm>
#include <functional>
#include <cstdlib>
using namespace distill;
namespace {
struct Fake final : ConditionEvaluator {
  std::vector<ConditionInput> inputs;
  std::function<Result<bool>(const ConditionInput&)> answer = [](const auto&) { return true; };
  Result<bool> evaluate(const ConditionInput& in) override {
    inputs.push_back(in);
    // Pure logic tests remain in-memory locally. CI also checks every synthetic
    // input they construct, independently of the fake's chosen response.
    const char* mode = std::getenv("DISTILL_TEST_EVALUATOR");
    if (mode && std::string_view(mode) != "gcc") {
      test::TempDirectory temp; auto oracle = testing::testEvaluator(temp.path);
      if (oracle) (void)oracle->evaluate(in);
    }
    return answer(in);
  }
};
InlineResult raw(std::string text) {
  InlineResult r; r.text = std::move(text);
  std::size_t n = 1;
  for (std::size_t i = 0; i < r.text.size(); ++i) if (i == 0 || r.text[i - 1] == '\n') r.lineMap.push_back(SourceLocation{"main.cpp", n++});
  return r;
}
ResolveResult resolve(std::string source, ConditionEvaluator& e, ResolveOptions o = {}, std::vector<std::string> flags = {}) {
  std::vector<std::string> args{"g++", "main.cpp", "-std=gnu++20"}; args.insert(args.end(), flags.begin(), flags.end());
  auto c = parseCompileCommand(args, std::filesystem::current_path()).value();
  auto r = resolveConditions(raw(source), c, e, o);
  if (!r.ok()) throw std::runtime_error(r.error().message);
  return r.value();
}
Profile profile() {
  Profile p; p.reproduced = true; p.dialect = {"gnu++20", true, 202002};
  p.compiler = {CompilerFamily::gcc, 15, 2, 0}; p.libraries = {"glibc", "libstdc++"};
  return p;
}
Profile featureProfile(bool reproduced = false) {
  auto p = profile(); p.reproduced = reproduced; p.snapshot = "synthetic-empty-source.txt";
  p.triple = "x86_64-linux-gnu";
  p.snapshotFacts["__cpp_constexpr"] = {MacroState::known, Macro{false, {}, false, "201907L"}, {FactOrigin::compilerBuiltin}};
  // Exercise both explicit negative facts and names absent from the snapshot.
  p.snapshotFacts["__cpp_lib_ranges"] = {MacroState::undefined, {}, {FactOrigin::compilerBuiltin}};
  return p;
}
IntegerModel localIntegerModel(const std::filesystem::path& root) {
  const auto gcc = testing::testGcc(); if (!gcc) throw test::Skip{"GCC unavailable"};
  const auto command = parseCompileCommand({pathToUtf8(*gcc), "main.cpp", "-std=gnu++20", "-fsigned-char"}, root).value();
  const auto probe = probeCompiler(*gcc, command, {true, {}, root});
  if (!probe.ok()) throw std::runtime_error(probe.error().message);
  auto p = profile();
  for (const auto& [name, macro] : probe.value().macros)
    p.snapshotFacts[name] = {MacroState::known, macro, {FactOrigin::compilerBuiltin}};
  const auto model = profileRequirements(p).integers;
  if (!model) throw std::runtime_error("unknown local integer model");
  return *model;
}
const std::vector<std::string> libraryFeatureConditions{
  "#if defined(__cpp_lib_mdspan)", "#ifdef __cpp_lib_ranges", "#ifndef __cpp_lib_mdspan",
  "#if __cpp_lib_ranges >= 202110L", "#if defined(__cpp_lib_zzz_future)",
  "#ifdef __cpp_lib_zzz_future", "#ifndef __cpp_lib_zzz_future",
  "#if __cpp_lib_zzz_future >= 202110L"
};
std::string libraryFeatureChecks() {
  std::string source;
  for (const auto& condition : libraryFeatureConditions)
    source += condition + "\npresent\n#else\nabsent\n#endif\n";
  return source;
}
}
TEST(resolve_fake_closure_and_bound_parameters) {
  Fake e;
  auto r = resolve("#define V __GNUC__\n#if V\nx\n#endif\n", e);
  CHECK_EQ(r.decisions[0].decision, Decision::unknown); CHECK(e.inputs.empty());
  CHECK_EQ(r.decisions[0].reason, "unknown macro: __GNUC__");
  CHECK(!r.decisions[0].external.empty());
  resolve("#define F(x,...) (x && __VA_OPT__(1))\n#if F(1,unused)\nx\n#endif\n", e);
  CHECK(e.inputs.empty()); // unused is still conservatively part of the closure.
  ResolveOptions o; o.requirements.integers = IntegerModel{};
  r = resolve("#define F(x) (x)\n#if F(1)\nx\n#endif\n", e, o);
  CHECK_EQ(r.decisions[0].decision, Decision::trueValue); CHECK_EQ(e.inputs.size(), 1u);
  CHECK(e.inputs[0].source.find("#define F(x) ( x )") != std::string::npos);
  CHECK(e.inputs[0].source.find("#define x") == std::string::npos);
}
TEST(resolve_fake_defined_placeholder_and_paste) {
  auto p = profile();
  p.snapshotFacts["__has_include"] = {MacroState::definedUnknown, {}, {FactOrigin::specialBuiltin}};
  ResolveOptions o; o.profile = &p;
  Fake e;
  auto r = resolve("#if defined(__has_include)\nx\n#endif\n", e, o);
  CHECK_EQ(r.decisions[0].decision, Decision::trueValue);
  CHECK(e.inputs[0].source.starts_with("#define __has_include 1\n"));
  CHECK(!r.decisions[0].external[0].definitionUsed);
  r = resolve("#if __has_include(<vector>)\nx\n#endif\n", e, o);
  CHECK_EQ(r.decisions[0].reason, "builtin invocation: __has_include");
  r = resolve("#define CAT(a,b) a ## b\n#if CAT(chosen,1)\nx\n#endif\n", e, o);
  CHECK_EQ(r.decisions[0].reason, "token paste");
  r = resolve("#define CAT(a,b) a ## b\n#if defined(CAT)\nx\n#endif\n", e, o);
  CHECK_EQ(r.decisions[0].decision, Decision::trueValue);
}
TEST(resolve_fake_disabled_and_a3) {
  Fake e;
  for (const auto* source : {"__COUNTER__\n#if 1\n#endif\n", "_Pragma(\"x\")\n", "#pragma push_macro(\"X\")\n", "#pragma pop_macro(\"X\")\n"}) {
    auto r = resolve(source, e); CHECK(!r.disabledReasons.empty()); CHECK_EQ(r.text, source);
  }
  auto r = resolve("#if 1\nx\n#endif\n", e, {}, {"-unknown"});
  CHECK_EQ(r.disabledReasons[0], "unmodelled option: -unknown");
  auto p = profile(); p.snapshotFacts["GUARD"] = {MacroState::known, Macro{false, {}, false, "1"}, {FactOrigin::compilerBuiltin}};
  ResolveOptions o; o.profile = &p;
  r = resolve("#define GUARD\n", e, o); CHECK(r.disabledReasons[0].find("GUARD") != std::string::npos);
  CHECK(e.inputs.empty());
  p = profile();
  p.snapshotFacts["__UNKNOWN"] = {MacroState::unknown, {}, {FactOrigin::nativeModel}};
  p.snapshotFacts["__X"] = {MacroState::known, Macro{false, {}, false, "1"}, {FactOrigin::compilerBuiltin}};
  r = resolve("#if __UNKNOWN\n#define __X 1\n#endif\n#if __X\nx\n#endif\n", e, o);
  CHECK_EQ(r.decisions[2].decision, Decision::trueValue);
  CHECK_EQ(r.decisions[2].external[0].name, "__X");
  CHECK(r.decisions[2].external[0].definitionUsed);
}
TEST(resolve_fake_external_model_and_errors) {
  Fake e;
  auto r = resolve("#if 1 + 2\nx\n#endif\n", e);
  CHECK_EQ(r.decisions[0].reason, "unknown integer model"); CHECK(e.inputs.empty());
  ResolveOptions o; o.requirements.integers = IntegerModel{};
  if (const auto* mode = std::getenv("DISTILL_TEST_EVALUATOR"); mode && std::string_view(mode) == "differential") {
    // This arithmetic fixture needs a known model, not an x86 wchar layout
    // that the native ARM64 GCC oracle cannot represent.
    test::TempDirectory temp; o.requirements.integers = localIntegerModel(temp.path);
  }
  r = resolve("#if 1 + 2\nx\n#endif\n", e, o);
  CHECK(r.decisions[0].requirements.integers.has_value());
  e.answer = [](const auto&) -> Result<bool> { return Error{"bad expression"}; };
  r = resolve("#if 1\nx\n#endif\n", e);
  CHECK_EQ(r.decisions[0].reason, "evaluator error: bad expression");
  CHECK(!evaluationTriple(IntegerModel{16, 32, 64, 64, 32, 64, true, true}).ok());
  auto p = profile();
  for (const auto& [name, value] : {std::pair{"__CHAR_BIT__", "8"}, {"__SIZEOF_INT__", "4"},
       {"__SIZEOF_LONG__", "8"}, {"__SIZEOF_LONG_LONG__", "8"}, {"__SIZEOF_WCHAR_T__", "4"}, {"__INTMAX_WIDTH__", "64"}})
    p.snapshotFacts[name] = {MacroState::known, Macro{false, {}, false, value}, {FactOrigin::compilerBuiltin}};
  CHECK(profileRequirements(p).integers.has_value());
  p.snapshotFacts["__CHAR_UNSIGNED__"] = {MacroState::known, Macro{false, {}, false, "1"}, {FactOrigin::commandOperation}};
  CHECK(!profileRequirements(p).integers.has_value());
}
TEST(resolve_fake_limits_and_directional_comparison) {
  Fake e; auto a = resolve("#ifdef LOCAL\nx\n#endif\n", e);
  auto p = profile(); ResolveOptions o; o.profile = &p;
  auto b = resolve("#ifdef LOCAL\nx\n#endif\n", e, o);
  CHECK(sameDecisions(a, b)); CHECK(!sameDecisions(b, a));
  auto c = parseCompileCommand({"g++", "main.cpp"}, std::filesystem::current_path()).value();
  o.maxNesting = 1;
  CHECK(!resolveConditions(raw("#if 1\n#if 1\n#endif\n#endif\n"), c, e, o).ok());
  o.maxNesting = 200; o.maxOutputBytes = 1;
  CHECK(!resolveConditions(raw("text\n"), c, e, o).ok());
  CHECK(!resolveConditions(raw("#if 1\n"), c, e).ok());
  CHECK(!resolveConditions(raw("#else\n"), c, e).ok());
}
TEST(resolve_real_evaluators) {
  test::TempDirectory temp; auto evaluator = testing::testEvaluator(temp.path);
  if (!evaluator) throw test::Skip{"GCC unavailable"};
  auto p = profile();
  auto gcc = testing::testGcc();
  if (!gcc) throw test::Skip{"GCC required to capture evaluator fixture model"};
  auto command = parseCompileCommand({pathToUtf8(*gcc), "main.cpp", "-std=gnu++20", "-fsigned-char"}, temp.path).value();
  auto probe = probeCompiler(*gcc, command, {true, {}, temp.path});
  if (!probe.ok()) throw std::runtime_error(probe.error().message);
  for (const auto& [name, macro] : probe.value().macros) p.snapshotFacts[name] = {MacroState::known, macro, {FactOrigin::compilerBuiltin}};
  ResolveOptions o; o.profile = &p; o.requirements = profileRequirements(p);
  const std::vector<std::pair<std::string, bool>> table{
    {"1", true}, {"0", false}, {"1 + 2 == 3", true}, {"-1 < 1U", false},
    {"(0xffffffffffffffffULL >> 63) == 1", true}, {"true && !false", true},
    {"defined(F) && F(2,3) == 5", true}, {"V(1,2) == 3", true},
    {"N(1+2) == 3", true}, {"EMPTY() == 7", true}, {"defined(UNKNOWN_DEFINED)", false},
    {"A", false}, {"F(0,0)", false},
    {"'\\xff' < 0", true}, {"L'\\xffff' > 0", true}, {"1 << 40", true}
  };
  for (const auto& [condition, expected] : table) {
    auto r = resolve("#define F(x,y) ((x)+(y))\n#define V(x,...) ((x)+(__VA_ARGS__))\n#define N(args...) (args)\n#define EMPTY() 7\n#define A B\n#define B A\n#if " + condition + "\nx\n#else\ny\n#endif\n", *evaluator, o);
    if (r.decisions[0].decision == Decision::unknown) throw std::runtime_error(condition + ": " + r.decisions[0].reason);
    CHECK_EQ(r.decisions[0].decision, expected ? Decision::trueValue : Decision::falseValue);
  }
}
TEST(resolve_real_walk_join_and_dead_regions) {
  test::TempDirectory temp; auto e = testing::testEvaluator(temp.path); if (!e) throw test::Skip{"GCC unavailable"};
  auto p = profile(); p.snapshotFacts["__UNKNOWN"] = {MacroState::unknown, {}, {FactOrigin::nativeModel}};
  ResolveOptions o; o.profile = &p; o.requirements.integers = IntegerModel{};
  auto r = resolve("#if __UNKNOWN\n#define MODE 1\n#if 1\nx\n#endif\n#elif 0\n#define MODE 1\n#else\n#define MODE 1\n#endif\n#if MODE\ny\n#endif\n", *e, o);
  CHECK_EQ(r.decisions[0].decision, Decision::unknown);
  CHECK_EQ(r.decisions[1].decision, Decision::trueValue);
  CHECK_EQ(r.decisions[3].decision, Decision::unknown); // elif after unknown
  CHECK_EQ(r.decisions[6].decision, Decision::trueValue); // equal alternative states
  CHECK(r.text.find("#elif 0\n") != r.text.npos); CHECK(r.text.find("#if MODE") == r.text.npos);
  r = resolve("#if __UNKNOWN\n#define MODE 1\n#endif\n#if MODE\nx\n#endif\n", *e, o);
  CHECK_EQ(r.decisions[2].decision, Decision::unknown); // implicit empty alternative
  r = resolve("#if 0\n#define MODE 1\n#if __UNKNOWN\nx\n#endif\n#else\n#define MODE 0\n#endif\n#if MODE\ny\n#endif\n", *e, o);
  CHECK_EQ(r.decisions[1].decision, Decision::dead); CHECK_EQ(r.decisions[5].decision, Decision::falseValue);
}
TEST(resolve_real_headers_and_pragmas) {
  test::TempDirectory temp; auto e = testing::testEvaluator(temp.path); if (!e) throw test::Skip{"GCC unavailable"};
  auto p = profile();
  p.snapshotFacts["__GNUC__"] = {MacroState::known, Macro{false, {}, false, "15"}, {FactOrigin::compilerBuiltin}};
  p.snapshotFacts["__STARTUP__"] = {MacroState::known, Macro{false, {}, false, "1"}, {FactOrigin::startupHeader}};
  ResolveOptions o; o.profile = &p; o.requirements.integers = IntegerModel{};
  auto r = resolve("#define MODE 1\n#include <cstddef>\n#if defined(__GNUC__) && MODE\nx\n#endif\n#ifdef assert\nx\n#endif\n#ifdef __STARTUP__\nx\n#endif\n#include <math.h>\n#ifdef M_PI\nx\n#endif\n#include <vendor.h>\n#if MODE\nx\n#endif\n", *e, o);
  CHECK_EQ(r.decisions[0].decision, Decision::trueValue);
  for (auto i : {2,4,6,8}) CHECK_EQ(r.decisions[i].decision, Decision::unknown);
  r = resolve("#pragma GCC target(\"avx2\")\n#ifdef __AVX2__\nx\n#endif\n#ifdef __OPTIMIZE__\nx\n#endif\n", *e, o);
  CHECK_EQ(r.decisions[0].decision, Decision::unknown); CHECK_EQ(r.decisions[2].decision, Decision::unknown);
}
TEST(resolve_real_external_identity_after_headers) {
  test::TempDirectory temp; auto e = testing::testEvaluator(temp.path);
  if (!e) throw test::Skip{"GCC unavailable"};
  auto p = profile(); ResolveOptions o; o.profile = &p;
  const std::string guard = "#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))\n#include <intrin.h>\n#endif\n";
  auto r = resolve("#include <cstdint>\n#include <vector>\n" + guard, *e, o);
  CHECK(!r.unknownInUserCode); CHECK_EQ(r.decisions.at(0).decision, Decision::falseValue);
  CHECK(r.text.find("intrin.h") == r.text.npos);
  CHECK_EQ(r.decisions.at(0).external.size(), 3u);
  for (const auto& use : r.decisions.at(0).external) {
    CHECK_EQ(use.fact.state, MacroState::undefined);
    CHECK_EQ(use.fact.provenance, (std::vector<FactOrigin>{FactOrigin::a4}));
  }
  // Local command operations cannot become judge facts.
  r = resolve("#include <cstdint>\n" + guard, *e, o, {"-D_MSC_VER=1930", "-D_M_X64=100"});
  CHECK_EQ(r.decisions.at(0).decision, Decision::falseValue);
  // Profile operations, in contrast, establish external facts, including -U.
  p.snapshotFacts["_MSC_VER"] = {MacroState::known, Macro{false, {}, false, "1930"}, {FactOrigin::commandOperation}};
  p.snapshotFacts["_M_X64"] = {MacroState::known, Macro{false, {}, false, "100"}, {FactOrigin::compilerBuiltin}};
  p.snapshotFacts["_M_ARM64"] = {MacroState::undefined, {}, {FactOrigin::commandOperation}};
  r = resolve("#include <cstdint>\n" + guard, *e, o);
  CHECK_EQ(r.decisions.at(0).decision, Decision::trueValue);
  // The live intrin.h then invalidates all facts, even known compiler names.
  r = resolve("#include <vendor.h>\n" + guard, *e, o);
  CHECK_EQ(r.decisions.at(0).decision, Decision::unknown);
  r = resolve("#include <cstdint>\n" + guard, *e);
  CHECK_EQ(r.decisions.at(0).decision, Decision::unknown);
  p = profile(); p.reproduced = false;
  r = resolve("#include <cstdint>\n" + guard, *e, o);
  CHECK_EQ(r.decisions.at(0).decision, Decision::unknown);
}
TEST(resolve_real_approximate_identity_respects_mingw_headers) {
  test::TempDirectory temp; auto e = testing::testEvaluator(temp.path);
  if (!e) throw test::Skip{"GCC unavailable"};
  for (const auto* selector : {"qoj", "cses", "codeforces", "timus"}) {
    auto loaded = lookupProfile(selector); CHECK(loaded.ok()); if (!loaded.ok()) continue;
    auto& p = loaded.value(); ResolveOptions o; o.profile = &p;
    const auto r = resolve("#ifdef _M_X64\nbefore\n#endif\n#include <cstdint>\n"
                           "#ifdef _MSC_VER\nmsvc\n#endif\n#ifdef _M_X64\nafter\n#endif\n", *e, o);
    CHECK_EQ(r.decisions.at(0).decision, Decision::falseValue);
    CHECK_EQ(r.decisions.at(2).decision, Decision::falseValue);
    CHECK_EQ(r.decisions.at(4).decision, p.triple.find("mingw") != p.triple.npos ? Decision::unknown : Decision::falseValue);
    CHECK_EQ(r.decisions.at(0).external.at(0).fact.provenance, (std::vector{FactOrigin::allowList}));
  }
}
TEST(resolve_real_approximate_shipped_x86_identity_after_headers) {
  test::TempDirectory temp; auto e = testing::testEvaluator(temp.path);
  if (!e) throw test::Skip{"GCC unavailable"};
  const std::string guard = "#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)\nx86\n#endif\n";
  for (const auto* selector : {"qoj", "cses", "kattis"}) {
    auto loaded = lookupProfile(selector); CHECK(loaded.ok()); if (!loaded.ok()) continue;
    const auto& p = loaded.value(); CHECK(!p.reproduced);
    CHECK_EQ(p.initialState("__i386__").state, MacroState::undefined);
    CHECK(!p.snapshotFacts.contains("__i386__"));
    ResolveOptions o; o.profile = &p;
    auto r = resolve("#include <cstdint>\n#ifdef __i386__\ni386_only\n#endif\n" + guard, *e, o);
    CHECK(!r.unknownInUserCode);
    CHECK_EQ(r.decisions.at(0).decision, Decision::falseValue);
    CHECK_EQ(r.decisions.at(0).external.at(0).fact.state, MacroState::undefined);
    CHECK_EQ(r.decisions.at(0).external.at(0).fact.provenance, (std::vector{FactOrigin::allowList}));
    CHECK_EQ(r.decisions.at(2).decision, Decision::trueValue);
    CHECK_EQ(r.decisions.at(2).external.size(), 4u);
    CHECK(r.text.find("i386_only") == r.text.npos); CHECK(r.text.find("#if") == r.text.npos);
    // Non-reserved i386 already has A5/A2 protection without an identity entry.
    r = resolve("#include <cstdint>\n#ifdef i386\nx\n#endif\n", *e, o);
    CHECK_EQ(r.decisions.at(0).decision, Decision::falseValue);
    for (const auto* write : {"#undef __i386__\n", "#define __i386__ 1\n"}) {
      r = resolve(std::string(write) + "#include <cstdint>\n#ifdef __i386__\n#endif\n" + guard, *e, o);
      CHECK_EQ(r.decisions.at(0).decision, Decision::unknown);
      // The kept __x86_64__ fact alone decides the guard (boolean precision).
      CHECK_EQ(r.decisions.at(2).decision, Decision::trueValue);
      CHECK_EQ(r.decisions.at(2).external.size(), 3u);
    }
    r = resolve("#include <vendor.h>\n#include <cstdint>\n" + guard, *e, o);
    CHECK_EQ(r.decisions.at(0).decision, Decision::unknown);
  }
  const auto r = resolve("#include <cstdint>\n" + guard, *e);
  CHECK_EQ(r.decisions.at(0).decision, Decision::unknown);
}
TEST(resolve_fake_identity_source_writes_and_library_restrictions) {
  Fake e; auto p = profile(); ResolveOptions o; o.profile = &p;
  for (const auto* write : {"#define _MSC_VER 1930\n", "#undef _MSC_VER\n"}) {
    auto r = resolve(std::string(write) + "#ifdef _MSC_VER\nbefore\n#endif\n#include <cstdint>\n#ifdef _MSC_VER\nafter\n#endif\n", e, o);
    CHECK_EQ(r.decisions.at(2).decision, Decision::unknown);
    CHECK(r.decisions.at(2).external.empty());
  }
  // Equal source and compiler definitions joined across an unknown branch
  // must not acquire external-only protection at the following include.
  p.snapshotFacts["_MSC_VER"] = {MacroState::known, Macro{false, {}, false, "1930"}, {FactOrigin::compilerBuiltin}};
  p.snapshotFacts["__UNKNOWN"] = {MacroState::unknown, {}, {FactOrigin::nativeModel}};
  auto r = resolve("#if __UNKNOWN\n#define _MSC_VER 1930\n#endif\n#include <cstdint>\n#ifdef _MSC_VER\nx\n#endif\n", e, o);
  CHECK_EQ(r.decisions.at(2).decision, Decision::unknown);
  for (const auto& libraries : {std::vector<std::string>{"mingw-w64", "libstdc++"}, {"missing"}, {}}) {
    p = profile(); p.libraries = libraries;
    r = resolve("#include <cstdint>\n#ifdef _M_X64\nx\n#endif\n", e, o);
    CHECK_EQ(r.decisions.at(0).decision, Decision::unknown);
  }
  // No header exemption may restore a fact lost at an earlier nonstandard one.
  p = profile();
  r = resolve("#include <vendor.h>\n#include <cstdint>\n#ifdef _MSC_VER\nx\n#endif\n", e, o);
  CHECK_EQ(r.decisions.at(0).decision, Decision::unknown);
}
TEST(resolve_real_library_features_before_include) {
  test::TempDirectory temp; auto e = testing::testEvaluator(temp.path); if (!e) throw test::Skip{"GCC unavailable"};
  auto p = featureProfile();
  ResolveOptions o; o.profile = &p; o.requirements.integers = localIntegerModel(temp.path);
  const auto r = resolve(libraryFeatureChecks(), *e, o);
  CHECK(r.disabledReasons.empty());
  CHECK_EQ(r.decisions.size(), 3 * libraryFeatureConditions.size());
  for (std::size_t i = 0; i < libraryFeatureConditions.size(); ++i) {
    const auto& d = r.decisions.at(3 * i);
    const bool negated = libraryFeatureConditions[i].starts_with("#ifndef");
    CHECK_EQ(d.reason, "");
    CHECK_EQ(d.decision, negated ? Decision::trueValue : Decision::falseValue);
    CHECK_EQ(d.external.size(), 1u);
    const auto& fact = d.external.at(0).fact;
    CHECK_EQ(fact.state, MacroState::undefined);
    CHECK(std::find(fact.provenance.begin(), fact.provenance.end(), FactOrigin::allowList) != fact.provenance.end());
    CHECK(r.text.find(libraryFeatureConditions[i]) == std::string::npos);
  }
}
TEST(resolve_fake_library_features_after_system_includes) {
  // Reserved names are covered independently of the finite A1 inventories.
  CHECK(!isStandardMacroName("__cpp_lib_zzz_future"));
  for (const auto& [library, names] : embeddedExtensionLists()) {
    (void)library;
    CHECK(!names.contains("__cpp_lib_zzz_future"));
  }
  for (const bool reproduced : {false, true}) {
    auto p = featureProfile(reproduced);
    ResolveOptions o; o.profile = &p; o.requirements.integers = IntegerModel{};
    for (const auto* header : {"version", "bits/stdc++.h", "vector"}) {
      CHECK(isStandardHeader(header));
      Fake e;
      const auto source = "#include <" + std::string(header) + ">\n" + libraryFeatureChecks();
      const auto r = resolve(source, e, o);
      CHECK(r.disabledReasons.empty()); CHECK(r.unknownInUserCode);
      CHECK_EQ(r.text, source); CHECK(e.inputs.empty());
      CHECK_EQ(r.decisions.size(), 3 * libraryFeatureConditions.size());
      for (std::size_t i = 0; i < libraryFeatureConditions.size(); ++i) {
        const auto& d = r.decisions.at(3 * i);
        CHECK_EQ(d.decision, Decision::unknown);
        CHECK(d.reason.starts_with("unknown macro: __cpp_lib_"));
        CHECK(d.external.empty()); // Invalidated by the header, not a snapshot use.
      }
    }
  }
}
TEST(resolve_fake_library_features_via_user_header) {
  test::TempDirectory temp;
  temp.write("main.cpp", "#include \"features.h\"\n" + libraryFeatureChecks());
  temp.write("features.h", "#include <version>\n" + libraryFeatureChecks());
  const auto command = parseCompileCommand({"g++", "main.cpp", "-std=gnu++20"}, temp.path).value();
  const auto search = buildIncludeSearch(command, {}).value();
  const auto inlined = inlineSources(command, search);
  CHECK(inlined.ok()); if (!inlined.ok()) return;
  CHECK_EQ(inlined.value().userFiles.size(), 2u);
  CHECK_EQ(inlined.value().systemIncludes.size(), 1u);
  CHECK_EQ(inlined.value().systemIncludes.at(0).spelling, "version");
  CHECK_EQ(inlined.value().systemIncludes.at(0).location.file, "features.h");
  auto p = featureProfile();
  ResolveOptions o; o.profile = &p; o.requirements.integers = IntegerModel{};
  Fake e;
  const auto resolved = resolveConditions(inlined.value(), command, e, o);
  CHECK(resolved.ok()); if (!resolved.ok()) return;
  const auto& r = resolved.value();
  CHECK(r.disabledReasons.empty()); CHECK(r.unknownInUserCode); CHECK(e.inputs.empty());
  CHECK_EQ(r.text, inlined.value().text); CHECK_EQ(r.lineMap, inlined.value().lineMap);
  CHECK_EQ(r.decisions.size(), 6 * libraryFeatureConditions.size());
  for (std::size_t i = 0; i < 2 * libraryFeatureConditions.size(); ++i) {
    const auto& d = r.decisions.at(3 * i);
    CHECK_EQ(d.decision, Decision::unknown);
    CHECK(d.reason.starts_with("unknown macro: __cpp_lib_")); CHECK(d.external.empty());
  }
}
TEST(resolve_real_language_feature_after_standard_include) {
  test::TempDirectory temp; auto e = testing::testEvaluator(temp.path); if (!e) throw test::Skip{"GCC unavailable"};
  auto p = featureProfile();
  ResolveOptions o; o.profile = &p; o.requirements.integers = localIntegerModel(temp.path);
  // docs/design.md A2(a) explicitly assumes standard headers preserve compiler
  // predefines. This exemption covers __cpp_constexpr, not absent __cpp_lib_*.
  const auto r = resolve("#include <version>\n#ifdef __cpp_constexpr\nyes\n#endif\n"
                         "#if __cpp_constexpr == 201907L\nyes\n#endif\n"
                         "#if __cpp_constexpr > 201907L\nno\n#endif\n", *e, o);
  CHECK(r.disabledReasons.empty());
  CHECK_EQ(r.decisions.at(0).decision, Decision::trueValue);
  CHECK_EQ(r.decisions.at(2).decision, Decision::trueValue);
  CHECK_EQ(r.decisions.at(4).decision, Decision::falseValue);
  for (const auto i : {0u, 2u, 4u}) {
    CHECK_EQ(r.decisions.at(i).reason, "");
    const auto& use = r.decisions.at(i).external.at(0);
    CHECK_EQ(use.name, "__cpp_constexpr"); CHECK_EQ(use.fact.state, MacroState::known);
    CHECK_EQ(use.fact.definition->replacement, "201907L");
  }
}
TEST(resolve_real_local_and_shipped_profiles) {
  test::TempDirectory temp; auto e = testing::testEvaluator(temp.path); if (!e) throw test::Skip{"GCC unavailable"};
  auto a = resolve("#ifdef LOCAL\nx\n#endif\n", *e);
  CHECK_EQ(a.decisions[0].decision, Decision::unknown);
  for (const auto* name : {"codeforces-gcc14-cpp23", "atcoder-gcc15-gnupp23"}) {
    auto p = lookupProfile(name); CHECK(p.ok()); if (!p.ok()) continue;
    ResolveOptions o; o.profile = &p.value();
    auto b = resolve("#ifdef LOCAL\nx\n#endif\n#ifdef ONLINE_JUDGE\ny\n#endif\n", *e, o);
    CHECK_EQ(b.decisions[0].decision, Decision::falseValue); CHECK_EQ(b.decisions[2].decision, Decision::trueValue);
  }
}
TEST(resolve_fake_a3_approximate_snapshot_and_generated_unknown) {
  Fake e;
  auto p = profile(); p.reproduced = false;
  p.snapshotFacts["HIDDEN"] = {MacroState::known, Macro{false, {}, false, "1"}, {FactOrigin::compilerBuiltin}};
  ResolveOptions o; o.profile = &p;
  auto r = resolve("#define HIDDEN 1\n", e, o);
  CHECK(!r.disabledReasons.empty()); CHECK(r.disabledReasons[0].find("HIDDEN") != r.disabledReasons[0].npos);
  auto c = parseCompileCommand({"g++", "main.cpp"}, std::filesystem::current_path()).value();
  auto input = raw("#ifdef GENERATED\ntext\n#endif\n");
  for (auto& line : input.lineMap) line.reset();
  auto unknown = resolveConditions(input, c, e);
  CHECK(unknown.ok()); CHECK(unknown.value().unknownInUserCode);
}
TEST(resolve_generated_lines_use_original_presumed_coordinates) {
  Fake e;
  auto c = parseCompileCommand({"g++", "entry.cpp"}, std::filesystem::current_path()).value();
  auto input = raw("#if 1\n#define GENERATED\n#endif\n");
  for (auto& line : input.lineMap) line.reset();
  auto resolved = resolveConditions(input,c,e);
  CHECK(resolved.ok());
  CHECK_EQ(resolved.value().text,"#line 2 \"entry.cpp\"\n#define GENERATED\n#line 4\n");

  // Only actual generated directives reset coordinates. The marker-looking
  // line inside this literal must not change the later generated define.
  input = raw("#line 20 \"lib/ds.h\"\nauto s=R\"(\n#line 99 \"raw.cpp\"\n)\";\n"
              "#if 1\n#define GENERATED\n#endif\n");
  for (auto& line : input.lineMap) line.reset();
  resolved = resolveConditions(input,c,e);
  CHECK(resolved.ok());
  CHECK(resolved.value().text.starts_with("#line 20 \"lib/ds.h\"\n"));
  CHECK(resolved.value().text.find("\n#line 24\n#define GENERATED") != std::string::npos);
}
TEST(resolve_fake_spliced_function_and_multiline_macro) {
  Fake e; ResolveOptions o; o.requirements.integers = IntegerModel{};
  auto r = resolve("#define F\\\n(x) (x)\n#if F(1)\nx\n#endif\n", e, o);
  CHECK_EQ(r.decisions[0].decision, Decision::trueValue);
  CHECK(e.inputs[0].source.starts_with("#define F(x)"));
  r = resolve("#define TEXT R\"(one\ntwo)\"\n#if defined(TEXT)\nx\n#endif\n", e);
  CHECK_EQ(r.decisions[0].decision, Decision::trueValue);
}
TEST(resolve_real_isolation_and_rebound_builtins) {
  test::TempDirectory temp; auto e = testing::testEvaluator(temp.path); if (!e) throw test::Skip{"GCC unavailable"};
  for (const auto* condition : {"defined(__GNUC__)", "defined(__clang__)", "defined(__cplusplus)", "defined(__LINE__)", "defined(__has_include)", "defined(__is_target_arch)", "defined(__has_constexpr_builtin)"}) {
    ConditionInput input{"#if " + std::string(condition) + "\nDISTILL_CONDITION_TRUE\n#else\nDISTILL_CONDITION_FALSE\n#endif\n", {}};
    auto r = e->evaluate(input); CHECK(r.ok()); if (r.ok()) CHECK(!r.value());
  }
  auto p = profile(); ResolveOptions o; o.profile = &p;
  auto r = resolve("#define __has_include(x) 1\n#if __has_include(1)\nx\n#endif\n", *e, o);
  CHECK_EQ(r.decisions[0].decision, Decision::trueValue);
  r = resolve("#define DISTILL_CONDITION_TRUE DISTILL_CONDITION_FALSE\n#define DISTILL_CONDITION_FALSE 1\n#if DISTILL_CONDITION_TRUE\nx\n#endif\n", *e, o);
  CHECK_EQ(r.decisions[0].decision, Decision::trueValue);
  r = resolve("#undef __LINE__\n#if __LINE__\nx\n#endif\n", *e, o);
  CHECK_EQ(r.decisions[0].decision, Decision::falseValue);
}

namespace {
InlineResult libraryRaw(std::string source, bool own = true, bool found = true) {
  auto r = raw(std::move(source));
  const auto scan = scanDirectives(r.text).value();
  for (const auto& line : scan.lines) if (line.directive && line.directive->name == "include") {
    const auto& operand = line.directive->tokens.front().text;
    r.systemIncludes.push_back({{"main.cpp", line.tokens.front().line}, operand.substr(1, operand.size() - 2),
                               operand.front() == '<', false, found, true, {}, own});
  }
  return r;
}
ResolveResult libraryResolve(std::string source, ConditionEvaluator& e, const ResolveOptions& o,
                             std::vector<std::string> flags = {}, bool own = true, bool found = true) {
  std::vector<std::string> args{"g++", "main.cpp", "-std=gnu++23"};
  args.insert(args.end(), flags.begin(), flags.end());
  auto command = parseCompileCommand(args, std::filesystem::current_path()).value();
  return resolveConditions(libraryRaw(std::move(source), own, found), command, e, o).value();
}
const std::string toCharsCheck = "#if defined(__cpp_lib_to_chars)\nyes\n#else\nno\n#endif\n";
}
TEST(resolve_library_shipped_acceptance_and_provenance) {
  test::TempDirectory temp; auto e = testing::testEvaluator(temp.path); if (!e) throw test::Skip{"GCC unavailable"};
  const auto integers = localIntegerModel(temp.path);
  for (const auto* judge : {"cses", "codeforces"}) {
    auto p = lookupProfile(judge).value(); ResolveOptions o; o.profile = &p;
    const auto& data = embeddedLibraryData();
    CHECK_EQ(data.selector(p), judge == std::string_view("cses") ? "gcc-13" : "gcc-14");
    auto r = libraryResolve("#include <charconv>\n" + toCharsCheck, *e, o);
    CHECK_EQ(r.decisions[0].decision, Decision::trueValue); CHECK(!r.unknownInUserCode);
    CHECK_EQ(r.decisions[0].external[0].fact.provenance, (std::vector{FactOrigin::library}));
    CHECK_EQ(r.decisions[0].external[0].fact.definition->replacement, "201611L");
    CHECK_EQ(r.libraryProvenance.size(), 1u);
    CHECK(r.libraryProvenance.begin()->find(data.provenance) != std::string::npos);
    CHECK(r.libraryProvenance.begin()->find("selector=" + data.selector(p)) != std::string::npos);
    // Check actual replacement arithmetic with a model the local oracle can
    // represent (the Codeforces profile itself has MinGW's LLP64 layout).
    o.requirements.integers = integers;
    r = libraryResolve("#include <charconv>\n#if __cpp_lib_to_chars >= 201611L\nx\n#endif\n", *e, o);
    CHECK_EQ(r.decisions[0].decision, Decision::trueValue);
    CHECK(r.decisions[0].external[0].definitionUsed);
    // Facts are transitions, never initial definitions or identity exemptions.
    r = libraryResolve(toCharsCheck, *e, o);
    CHECK_EQ(r.decisions[0].decision, Decision::falseValue); CHECK(r.libraryProvenance.empty());
    r = libraryResolve("#include <charconv>\n#include <vendor.h>\n" + toCharsCheck, *e, o);
    CHECK_EQ(r.decisions[0].decision, Decision::unknown);
    for (const bool own : {false, true}) {
      r = libraryResolve("#include <charconv>\n" + toCharsCheck, *e, o, {}, own, false);
      CHECK_EQ(r.decisions[0].decision, Decision::unknown);
    }
    r = libraryResolve("#include <charconv>\n" + toCharsCheck, *e, o, {}, false);
    CHECK_EQ(r.decisions[0].decision, Decision::unknown);
  }
  for (const unsigned major : {14, 15, 16}) {
    auto p = featureProfile(); p.compiler.major = major;
    ResolveOptions o; o.profile = &p;
    auto r = libraryResolve("#include <thread>\n#ifdef __cpp_lib_jthread\nx\n#endif\n", *e, o);
    CHECK_EQ(r.decisions[0].decision, Decision::unknown); CHECK(r.libraryProvenance.empty());
  }
}
TEST(resolve_library_all_bundled_profiles) {
  test::TempDirectory temp; auto e = testing::testEvaluator(temp.path); if (!e) throw test::Skip{"GCC unavailable"};
  const auto catalog = listProfiles().value();
  const auto& data = embeddedLibraryData();
  CHECK(!catalog.profiles.empty());
  const auto integers = localIntegerModel(temp.path);
  for (const auto& p : catalog.profiles) {
    std::string reason;
    const auto selector = data.selector(p, false, &reason);
    CHECK(!selector.empty()); CHECK(reason.empty());
    CHECK_EQ(selector, p.reproduced ? "profile-" + p.name : "gcc-" + std::to_string(p.compiler.major));
    ResolveOptions o; o.profile = &p; o.requirements.integers = integers;
    auto r = libraryResolve("#include <charconv>\n" + toCharsCheck, *e, o);
    CHECK_EQ(r.decisions.at(0).decision, Decision::trueValue);
    CHECK(!r.unknownInUserCode); CHECK(!r.libraryProvenance.empty());
    const auto* header = data.find(selector, "cfloat");
    CHECK(header); if (!header) continue;
    const auto value = data.state(*header, "FLT_EVAL_METHOD");
    // Captured integers still require audited independence/native coverage.
    // The real generator-5 matrix currently marks this fact unknown.
    const auto literal = value >= 2 ? std::string(data.values[value]) : "0";
    r = libraryResolve("#include <cfloat>\n#if defined(FLT_EVAL_METHOD) && FLT_EVAL_METHOD == " + literal + "\nx\n#endif\n", *e, o);
    CHECK_EQ(r.decisions.at(0).decision, value >= 2 ? Decision::trueValue : value == 0 ? Decision::falseValue : Decision::unknown);
    std::cout << p.name << ": selector=" << selector << ", __cpp_lib_to_chars=known, FLT_EVAL_METHOD="
              << (value >= 2 ? literal : value == 0 ? "absent" : "unknown (baseline audit)") << '\n';
  }
}
TEST(resolve_library_satisfaction_feature_agreement_keeps_standard_target_checks) {
  test::TempDirectory temp; auto e = testing::testEvaluator(temp.path); if (!e) throw test::Skip{"GCC unavailable"};
  constexpr std::string_view names[]{"FLT_EVAL_METHOD", "__cpp_lib_to_chars"}, values[]{"", "", "0", "201611L"};
  constexpr std::uint16_t cfloat[]{2, 0}, charconv[]{0, 3};
  constexpr std::string_view flags[]{"-O2", "-DONLINE_JUDGE"};
  const LibraryHeader headers[]{{"gcc-14", "cfloat", cfloat}, {"gcc-14", "charconv", charconv}};
  const LibraryBuild builds[]{{"", "14", "14.2.0", "c++23", "x86_64-w64-mingw32", "", flags}};
  LibraryData data{names, values, headers, builds, "synthetic"}; data.exactFlags = true;
  auto p = lookupProfile("codeforces").value();
  ResolveOptions o; o.profile = &p; o.libraryData = &data;
  const auto source = "#include <charconv>\n" + toCharsCheck + "#include <cfloat>\n#ifdef FLT_EVAL_METHOD\nx\n#endif\n";
  auto r = libraryResolve(source, *e, o);
  CHECK_EQ(r.decisions.at(0).decision, Decision::trueValue);
  CHECK_EQ(r.decisions.at(3).decision, Decision::trueValue);
  for (const auto* triple : {"x86_64-linux-gnu", "x86_64-apple-darwin"}) {
    p.triple = triple;
    CHECK(data.selector(p, true).empty());
    // Judge resolution itself keeps the exact target requirement.
    r = libraryResolve(source, *e, o);
    CHECK_EQ(r.decisions.at(0).decision, Decision::unknown);
    o.libraryAgreementOnly = true;
    r = libraryResolve(source, *e, o);
    CHECK_EQ(r.decisions.at(0).decision, Decision::trueValue);
    CHECK_EQ(r.decisions.at(3).decision, Decision::unknown);
    CHECK(r.libraryProvenance.contains("synthetic selector=gcc-14"));
    PipelineResult report; report.resolution = r; report.assumptions.insert("A9");
    const auto command = parseCompileCommand({"g++", "main.cpp"}, temp.path).value();
    const auto text = renderReport(report, command, &p);
    CHECK(text.find("library facts: none") == std::string::npos);
    CHECK(text.find("standard library macro facts: unavailable (") != std::string::npos);
    p.flags.push_back("-ffreestanding");
    r = libraryResolve(source, *e, o);
    CHECK_EQ(r.decisions.at(0).decision, Decision::unknown);
    p.flags.pop_back(); o.libraryAgreementOnly = false;
  }
  p = lookupProfile("codeforces").value(); p.flags = {"-DONLINE_JUDGE", "-O2"};
  CHECK(data.selector(p, true).empty());
  o.libraryAgreementOnly = true;
  r = libraryResolve(source, *e, o);
  CHECK_EQ(r.decisions.at(0).decision, Decision::trueValue);
  CHECK_EQ(r.decisions.at(3).decision, Decision::unknown);
}
TEST(resolve_library_default_configuration_monotone_and_entry) {
  test::TempDirectory temp; auto e = testing::testEvaluator(temp.path); if (!e) throw test::Skip{"GCC unavailable"};
  auto p = lookupProfile("codeforces").value(); ResolveOptions o; o.profile = &p;
  for (const auto* change : {"#define _GLIBCXX_USE_CXX11_ABI 0\n", "#undef _GLIBCXX_USE_CXX11_ABI\n",
       "#define __cpp_lib_to_chars 201611L\n", "#undef __cpp_lib_to_chars\n", "#include <vendor.h>\n",
       "#pragma GCC target(\"avx2\")\n", "#pragma GCC optimize(\"O3\")\n",
       "#pragma GCC push_options\n", "#pragma GCC pop_options\n"}) {
    for (const auto& prefix : {std::string{}, std::string("#include <charconv>\n")}) {
      auto r = libraryResolve(prefix + change + "#include <charconv>\n" + toCharsCheck, *e, o);
      CHECK_EQ(r.decisions[0].decision, Decision::unknown);
    }
  }
  // Repeating an equal source definition or undefinition cannot restore default configuration.
  auto r = libraryResolve("#define __X 1\n#undef __X\n#include <charconv>\n" + toCharsCheck, *e, o);
  CHECK_EQ(r.decisions[0].decision, Decision::unknown);
  for (const auto* flag : {"-D__X=1", "-U__X", "-D_GLIBCXX_USE_CXX11_ABI=1"}) {
    r = libraryResolve("#include <charconv>\n" + toCharsCheck, *e, o, {flag});
    CHECK_EQ(r.decisions[0].decision, Decision::unknown);
  }
  r = libraryResolve("#define local 1\n#undef local\n#include <charconv>\n" + toCharsCheck, *e, o, {"-DOTHER=1"});
  CHECK_EQ(r.decisions[0].decision, Decision::trueValue);
  for (const auto* flag : {"-include", "-imacros", "-includeforced.h", "-imacrosforced.h"}) {
    // The command/profile parsers still reject these. Defend the transition even
    // for an internal caller with an already constructed command/profile.
    p.flags.push_back(flag);
    r = libraryResolve("#include <charconv>\n" + toCharsCheck, *e, o);
    CHECK_EQ(r.decisions[0].decision, Decision::unknown);
    p.flags.pop_back();
    auto command = parseCompileCommand({"g++", "main.cpp"}, temp.path).value();
    command.args.push_back({{flag}, Class::modelled, flag, "forced.h"});
    r = resolveConditions(libraryRaw("#include <charconv>\n" + toCharsCheck), command, *e, o).value();
    CHECK_EQ(r.decisions[0].decision, Decision::unknown);
    CHECK(!parseCompileCommand({"g++", "main.cpp", flag}, temp.path).ok());
  }
  for (const auto* pragma : {"push_macro", "pop_macro"}) {
    r = libraryResolve(std::string("#pragma ") + pragma + "(\"ordinary\")\n#include <charconv>\n" + toCharsCheck, *e, o);
    CHECK(!r.disabledReasons.empty()); CHECK_EQ(r.decisions[0].decision, Decision::unknown);
  }
  o.libraryData = nullptr;
  r = libraryResolve("#include <charconv>\n" + toCharsCheck, *e, o);
  CHECK_EQ(r.decisions[0].decision, Decision::unknown);
}
TEST(resolve_library_agreement_requires_scanned_dialect_and_flags) {
  test::TempDirectory temp; auto e = testing::testEvaluator(temp.path); if (!e) throw test::Skip{"GCC unavailable"};
  const auto& data = embeddedLibraryData();
  auto p = featureProfile(); p.provenance.origin = "user";
  p.dialect = {"c++23", false, 202302};
  ResolveOptions o; o.profile = &p;
  CHECK_EQ(data.selector(p), "gcc-15");
  auto r = libraryResolve("#include <version>\n#ifdef __cpp_lib_ranges\nx\n#endif\n", *e, o);
  CHECK_EQ(r.decisions[0].decision, Decision::trueValue);
  p.dialect = {"c++17", false, 201703};
  r = libraryResolve("#include <version>\n#ifdef __cpp_lib_ranges\nx\n#endif\n", *e, o);
  CHECK_EQ(r.decisions[0].decision, Decision::unknown);
  CHECK(data.selector(p).empty()); CHECK(r.libraryProvenance.empty());
  CHECK_EQ(r.libraryDisabledReason, "profile dialect not covered by scanned builds: c++17");
  p.dialect = {"c++23", false, 202302};
  for (const auto* flag : {"-ffreestanding", "-fno-exceptions", "-fno-rtti", "-D_GLIBCXX_DEBUG",
                          "-U__EXCEPTIONS", "-Wp,-D_GLIBCXX_DEBUG", "-O0", "-O3", "-Os", "-Og"}) {
    p.flags = {flag};
    r = libraryResolve("#include <charconv>\n" + toCharsCheck, *e, o);
    CHECK_EQ(r.decisions[0].decision, Decision::unknown);
    CHECK(data.selector(p).empty()); CHECK(r.libraryProvenance.empty());
    CHECK_EQ(r.libraryDisabledReason, (data.exactFlags ? "profile ordered flags not matched by a scanned build: " :
                                                     "profile flags not covered by scanned builds: ") + std::string(flag));
    PipelineResult report; report.resolution = r;
    const auto command = parseCompileCommand({"g++", "main.cpp"}, temp.path).value();
    CHECK(renderReport(report, command, &p).find("library facts: none (" + r.libraryDisabledReason + ")\n") != std::string::npos);
  }
  // Match flags within the same dialect, never borrowing from another dialect.
  p.flags = {"-fmodules"}; CHECK(data.selector(p).empty());
  p.dialect = {"gnu++23", true, 202302};
  CHECK_EQ(data.selector(p), data.exactFlags ? "" : "gcc-15");
  // Debug and diagnostic flags are harmless; optimization still needs coverage.
  p = lookupProfile("codeforces").value(); p.provenance.origin = "user";
  p.flags.push_back("-Wshadow");
  CHECK_EQ(data.selector(p), "gcc-14");
  r = libraryResolve("#include <charconv>\n" + toCharsCheck, *e, o);
  CHECK_EQ(r.decisions[0].decision, Decision::trueValue); CHECK(!r.libraryProvenance.empty());
  for (const auto* flag : {"-g3", "-pipe", "-fconstexpr-depth=4096", "-fconstexpr-loop-limit=1048576",
                          "-fconstexpr-ops-limit=2097152", "-fdiagnostics-color=always"}) {
    p.flags.push_back(flag); CHECK_EQ(data.selector(p), "gcc-14"); p.flags.pop_back();
  }
  // The reproduced path still requires its exact identity, even for warnings.
  p = lookupProfile("atcoder").value(); p.flags.push_back("-Wshadow");
  CHECK(data.selector(p).empty()); CHECK_EQ(data.selector(p, true), "gcc-15");
}
TEST(resolve_library_agreement_rejects_flags_from_separate_builds) {
  constexpr std::string_view first[]{"-O2"}, second[]{"-fno-exceptions"};
  constexpr std::string_view both[]{"-O2", "-fno-exceptions"};
  const LibraryBuild builds[]{
    {"", "15", "15.2.0", "gnu++20", "", "", first},
    {"", "15", "15.2.0", "gnu++20", "", "", second},
    {"", "14", "14.2.0", "gnu++20", "", "", both},
    {"", "15", "15.2.0", "c++23", "", "", both}};
  const LibraryData data{{}, {}, {}, builds, "synthetic"};
  for (const bool reproduced : {false, true}) {
    auto p = featureProfile(reproduced);
    for (const bool harmless : {false, true}) {
      p.flags = {"-O2", "-fno-exceptions"};
      if (harmless) p.flags = {"-Wshadow", "-fno-exceptions", "-g3", "-O2", "-pipe"};
      std::string reason;
      CHECK(data.selector(p, reproduced, &reason).empty());
      CHECK_EQ(reason, std::string("profile flag combination not covered by a single scanned build: ") +
                       (harmless ? "-fno-exceptions -O2" : "-O2 -fno-exceptions"));
      CHECK(data.selector(p, reproduced).empty());
      // Individually missing flags keep the existing diagnostic and profile order.
      p.flags.insert(p.flags.begin(), "-fno-rtti");
      p.flags.push_back("-ffreestanding");
      CHECK(data.selector(p, reproduced, &reason).empty());
      CHECK_EQ(reason, "profile flags not covered by scanned builds: -fno-rtti -ffreestanding");
    }
  }
}
TEST(resolve_library_agreement_accepts_flags_in_one_build) {
  // Build flags may be reordered and may contain extra flags.
  constexpr std::string_view flags[]{"-fno-rtti", "-fno-exceptions", "-O2"};
  const LibraryBuild builds[]{{"", "15", "15.2.0", "gnu++20", "", "", flags}};
  const LibraryData data{{}, {}, {}, builds, "synthetic"};
  for (const bool reproduced : {false, true}) {
    auto p = featureProfile(reproduced);
    for (const auto& profileFlags : {std::vector<std::string>{"-O2", "-fno-exceptions"},
         std::vector<std::string>{"-Wshadow", "-O2", "-g3", "-fno-exceptions", "-pipe"},
         std::vector<std::string>{"-Wshadow", "-g3", "-pipe"}, std::vector<std::string>{}}) {
      p.flags = profileFlags;
      std::string reason = "stale reason";
      CHECK_EQ(data.selector(p, reproduced, &reason), "gcc-15");
      CHECK(reason.empty());
    }
  }
}
TEST(resolve_library_satisfaction_uses_configuration_coverage) {
  test::TempDirectory temp; auto e = testing::testEvaluator(temp.path); if (!e) throw test::Skip{"GCC unavailable"};
  auto p = featureProfile(); p.dialect = {"c++23", false, 202302};
  ResolveOptions o; o.profile = &p;
  const auto source = libraryRaw("#include <charconv>\n" + toCharsCheck);
  const auto command = parseCompileCommand({"g++", "main.cpp", "-std=c++23"}, temp.path).value();
  const auto resolution = resolveConditions(source, command, *e, o).value();
  CHECK_EQ(resolution.decisions[0].decision, Decision::trueValue);
  Configuration c; c.facts = p; c.facts.reproduced = true;
  CHECK(satisfies(c, source, command, resolution, *e).value());
  ResolveResult observed;
  c.facts.dialect = {"c++17", false, 201703};
  CHECK(!satisfies(c, source, command, resolution, *e, {}, &observed).value());
  CHECK_EQ(observed.decisions[0].decision, Decision::unknown);
  c.facts.dialect = p.dialect; c.facts.flags = {"-ffreestanding"};
  CHECK(!satisfies(c, source, command, resolution, *e, {}, &observed).value());
  // Generator 5 matches the entire ordered argv, including absent flags.
  CHECK_EQ(observed.libraryDisabledReason, "profile ordered flags not matched by a scanned build: -ffreestanding");
  c.facts.flags = {"-Wshadow"};
  CHECK(satisfies(c, source, command, resolution, *e).value());
}
TEST(resolve_library_joins_and_dead_paths) {
  test::TempDirectory temp; auto e = testing::testEvaluator(temp.path); if (!e) throw test::Skip{"GCC unavailable"};
  auto p = lookupProfile("codeforces").value(); ResolveOptions o; o.profile = &p;
  // Both macro states join to the same definition, but one path lost the Boolean.
  auto r = libraryResolve("#if __unknown\n#define __GNUC__ 14\n#endif\n#include <charconv>\n" + toCharsCheck, *e, o);
  CHECK_EQ(r.decisions[2].decision, Decision::unknown);
  r = libraryResolve("#if __unknown\n#define __X 1\n#endif\n#undef __X\n#include <charconv>\n" + toCharsCheck, *e, o);
  CHECK_EQ(r.decisions[2].decision, Decision::unknown);
  for (const auto* dead : {"#define __X 1\n", "#undef __X\n", "#include <vendor.h>\n", "#pragma GCC pop_options\n"}) {
    r = libraryResolve(std::string("#if 0\n") + dead + "#endif\n#include <charconv>\n" + toCharsCheck, *e, o);
    CHECK_EQ(r.decisions[2].decision, Decision::trueValue);
  }
  // Losing the flag on an unknown branch is enough, even when another branch
  // includes the same header and a later include repeats the same value.
  r = libraryResolve("#if __unknown\n#include <charconv>\n#else\n#define __X 1\n#endif\n#include <charconv>\n" + toCharsCheck, *e, o);
  CHECK_EQ(r.decisions[3].decision, Decision::unknown);
}
TEST(resolve_library_union_conflicts_absence_and_unknown_names) {
  test::TempDirectory temp; auto e = testing::testEvaluator(temp.path); if (!e) throw test::Skip{"GCC unavailable"};
  constexpr std::string_view names[]{"__cpp_lib_to_chars"}, values[]{"", "", "201611L", "202306L"};
  constexpr std::uint16_t positive[]{2}, conflict[]{3}, absent[]{0}, disagree[]{1};
  const LibraryHeader headers[]{{"gcc-15", "charconv", positive}, {"gcc-15", "cstddef", absent},
                                {"gcc-15", "ext/pb_ds/assoc_container.hpp", positive},
                                {"gcc-15", "thread", disagree}, {"gcc-15", "version", conflict}};
  const LibraryBuild builds[]{{"", "15", "15.2.0", "gnu++20", "", "", {}}};
  LibraryData data{names, values, headers, builds, "synthetic"};
  auto p = featureProfile(); ResolveOptions o; o.profile = &p; o.libraryData = &data;
  for (const auto* sequence : {"#include <charconv>\n#include <charconv>\n", "#include <charconv>\n#include <cstddef>\n",
       "#include <ext/pb_ds/assoc_container.hpp>\n"}) {
    auto r = libraryResolve(sequence + toCharsCheck, *e, o);
    CHECK_EQ(r.decisions[0].decision, Decision::trueValue);
  }
  for (const auto* sequence : {"#include <charconv>\n#include <version>\n#include <charconv>\n",
       "#include <charconv>\n#include <thread>\n#include <charconv>\n",
       "#if __unknown\n#include <charconv>\n#else\n#include <version>\n#endif\n#include <charconv>\n"}) {
    const auto r = libraryResolve(sequence + toCharsCheck, *e, o);
    const auto& d = r.decisions[r.decisions.size() - 3];
    CHECK_EQ(d.decision, Decision::unknown);
  }
  auto r = libraryResolve("#include <cstddef>\n" + toCharsCheck, *e, o);
  CHECK_EQ(r.decisions[0].decision, Decision::falseValue); // absence keeps the initial state
  CHECK_EQ(r.libraryProvenance.size(), 1u); // keeping it still relies on A9
  r = libraryResolve("#include <charconv>\n#ifdef __cpp_lib_unlisted_future\nx\n#endif\n", *e, o);
  CHECK_EQ(r.decisions[0].decision, Decision::unknown);
  p.reproduced = true; // No own build: never fall back to major facts.
  r = libraryResolve("#include <charconv>\n" + toCharsCheck, *e, o);
  CHECK_EQ(r.decisions[0].decision, Decision::unknown);
}
TEST(resolve_library_user_shadowing_and_compiler_directory_identity) {
  test::TempDirectory temp; auto e = testing::testEvaluator(temp.path); if (!e) throw test::Skip{"GCC unavailable"};
  std::filesystem::create_directory(temp.path / "user");
  std::filesystem::create_directory(temp.path / "compiler");
  temp.write("user/charconv", "// user shadow\n"); temp.write("compiler/charconv", "// library\n");
  temp.write("main.cpp", "#include <charconv>\n" + toCharsCheck);
  auto p = lookupProfile("codeforces").value(); ResolveOptions o; o.profile = &p;
  SearchLists probe{{{"compiler", IncludeKind::system, false, true}}};
  for (const auto* option : {"-I", "-isystem"}) {
    auto c = parseCompileCommand({"g++", "main.cpp", option, "user"}, temp.path).value();
    auto search = buildIncludeSearch(c, probe).value();
    auto hit = lookupHeader(search, "charconv", true, "main.cpp").value();
    CHECK(hit.found); CHECK(!hit.compilerSystem);
    const auto inlined = inlineSources(c, search).value();
    auto r = resolveConditions(inlined, c, *e, o).value();
    CHECK_EQ(r.decisions[0].decision, std::string_view(option) == "-I" ? Decision::falseValue : Decision::unknown);
    CHECK(r.libraryProvenance.empty());
  }
  // An equivalent -isystem spelling of an actual compiler directory qualifies.
  auto c = parseCompileCommand({"g++", "main.cpp", "-isystem", "./compiler"}, temp.path).value();
  auto search = buildIncludeSearch(c, probe).value();
  CHECK(lookupHeader(search, "charconv", true, "main.cpp").value().compilerSystem);
  auto inlined = inlineSources(c, search).value();
  CHECK(inlined.systemIncludes[0].compilerSystem);
  auto r = resolveConditions(inlined, c, *e, o).value();
  CHECK_EQ(r.decisions[0].decision, Decision::trueValue);
  // Metadata coordinates must survive quoted rewriting and multiline comments.
  temp.write("main.cpp", "/* prefix\n*/ #include \"charconv\"\n" + toCharsCheck);
  inlined = inlineSources(c, search).value();
  r = resolveConditions(inlined, c, *e, o).value();
  CHECK_EQ(r.decisions[0].decision, Decision::trueValue);
#ifndef _WIN32
  // A symlink escaping a compiler directory is not an installed library file.
  std::filesystem::remove(temp.path / "compiler/charconv");
  std::filesystem::create_symlink(temp.path / "user/charconv", temp.path / "compiler/charconv");
  CHECK(!lookupHeader(search, "charconv", true, "main.cpp").value().compilerSystem);
#endif
}
TEST(resolve_library_reproduced_profile_identity_and_own_snapshot) {
  test::TempDirectory temp; auto e = testing::testEvaluator(temp.path); if (!e) throw test::Skip{"GCC unavailable"};
  auto p = lookupProfile("atcoder").value(); ResolveOptions o; o.profile = &p;
  CHECK(p.reproduced);
  for (const auto* field : {"flags", "gcc", "build", "dialect", "triple", "name"}) {
    auto changed = p;
    if (std::string_view(field) == "flags") changed.flags.push_back("-D__X=1");
    if (std::string_view(field) == "gcc") changed.snapshotMetadata.gccVersion = "13.0.0";
    if (std::string_view(field) == "build") changed.snapshotMetadata.build = "different";
    if (std::string_view(field) == "dialect") changed.dialect.name = "c++20";
    if (std::string_view(field) == "triple") changed.triple = "x86_64-w64-mingw32";
    if (std::string_view(field) == "name") changed.name = "different";
    o.profile = &changed;
    const auto r = libraryResolve("#include <charconv>\n" + toCharsCheck, *e, o);
    CHECK_EQ(r.decisions[0].decision, Decision::unknown);
  }
  // Own snapshots may use build-dependent features; major agreement may not.
  p = lookupProfile("atcoder").value(); o.profile = &p;
  CHECK(p.reproduced);
  const auto r = libraryResolve("#include <thread>\n#ifdef __cpp_lib_jthread\nx\n#endif\n", *e, o);
  CHECK_EQ(r.decisions[0].decision, Decision::trueValue);
}
