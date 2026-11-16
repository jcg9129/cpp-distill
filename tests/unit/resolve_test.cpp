// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.h"
#include "core/resolve/resolve.h"
#include "evaluate.h"
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
  CHECK_EQ(resolved.value().text,"#line 2 \"entry.cpp\"\n#define GENERATED\n#line 4 \"entry.cpp\"\n");

  // Only actual generated directives reset coordinates. The marker-looking
  // line inside this literal must not change the later generated define.
  input = raw("#line 20 \"lib/ds.h\"\nauto s=R\"(\n#line 99 \"raw.cpp\"\n)\";\n"
              "#if 1\n#define GENERATED\n#endif\n");
  for (auto& line : input.lineMap) line.reset();
  resolved = resolveConditions(input,c,e);
  CHECK(resolved.ok());
  CHECK(resolved.value().text.find("#line 24 \"lib/ds.h\"\n#define GENERATED") != std::string::npos);
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
