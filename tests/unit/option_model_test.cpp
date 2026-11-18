// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.h"
#include "core/pipeline/pipeline.h"
#include "core/validate/preprocess.h"
#include "evaluate.h"
#include <algorithm>
#include <map>

using namespace distill;
namespace {
template<class T> T take(Result<T> r) {
  if (!r.ok()) throw std::runtime_error(r.error().message);
  return std::move(r.value());
}
InlineResult source(std::string text) {
  InlineResult r; r.text = std::move(text);
  for (std::size_t i = 0, line = 1; i < r.text.size(); ++i)
    if (i == 0 || r.text[i - 1] == '\n') r.lineMap.push_back(SourceLocation{"main.cpp", line++});
  return r;
}
std::string conditional(const std::string& expression) {
  return "#if " + expression + "\nint enabled;\n#else\nint disabled;\n#endif\nint main(){return 0;}\n";
}
struct LocalGcc {
  test::TempDirectory temp;
  std::filesystem::path gcc;
  std::unique_ptr<ConditionEvaluator> evaluator;
  std::map<std::vector<std::string>, Configuration> configurations;
  LocalGcc() {
    auto compiler = testing::testGcc(); if (!compiler) throw test::Skip{"GCC unavailable"};
    gcc = *compiler; evaluator = testing::testEvaluator(temp.path);
  }
  CompileCommand command(const std::vector<std::string>& flags, std::string_view dialect = "gnu++20") const {
    std::vector<std::string> argv{pathToUtf8(gcc), "-std=" + std::string(dialect), "main.cpp"};
    argv.insert(argv.end(), flags.begin(), flags.end());
    return take(parseCompileCommand(argv, temp.path));
  }
  Configuration probe(CompileCommand cmd) const {
    ProbeOptions po; po.temporaryRoot = temp.path;
    return take(probeConfiguration(std::move(cmd), "local", po));
  }
  const Configuration& configured(const std::vector<std::string>& flags) {
    if (!configurations.contains(flags)) configurations.emplace(flags, probe(command(flags)));
    return configurations.at(flags);
  }
  Profile profile(const std::vector<std::string>& flags) {
    auto p = configured(flags).facts;
    // Synthetic profile of this exact compiler, never a shipped x86 snapshot.
    p.flags = flags;
    if (!p.reproduced) throw test::Skip{"local GCC lacks a complete special-builtin registry"};
    return p;
  }
  void linuxOnly() {
    if (configured({}).probe.triple.find("linux") == std::string::npos)
      throw test::Skip{"Linux macro expectations"};
  }
};
}

TEST(option_model_target_compares_all_macros_even_for_harmless_flags) {
  Profile p; p.reproduced = true; p.compiler = {CompilerFamily::gcc, 15, 2, 0};
  p.dialect = {"gnu++20", true, 202002}; p.triple = "aarch64-linux-gnu";
  p.predefined = take(parseMacros("#define __DEPRECATED 1\n#define __FINITE_MATH_ONLY__ 0\n"
      "#define __NO_INLINE__ 1\n#define __cpp_runtime_arrays 198712L\n#define __cpp_binary_literals 201304L\n"));
  Configuration c; c.probe.identity = p.compiler; c.probe.triple = p.triple;
  c.probe.dialect = p.dialect; c.probe.macros = p.predefined; c.argv = {"g++"};
  for (const auto& name : specialBuiltins(p.compiler)) {
    p.snapshotFacts[name] = {MacroState::undefined, {}, {FactOrigin::specialBuiltin}};
    c.probe.special[name] = false;
  }
  CHECK(targetEquivalent(c, &p, false));
  struct Change { const char* flag; const char* macro; const char* definition; };
  for (const auto& row : std::vector<Change>{
      {"-O2", "__OPTIMIZE__", "1"}, {"-Os", "__OPTIMIZE_SIZE__", "1"},
      {"-O2", "__NO_INLINE__", nullptr}, {"-pthread", "_REENTRANT", "1"},
      {"-Wno-deprecated", "__DEPRECATED", nullptr}, {"-Wvla", "__cpp_runtime_arrays", nullptr},
      {"-pedantic", "__cpp_binary_literals", nullptr}, {"-Ofast", "__FINITE_MATH_ONLY__", "1"}}) {
    auto changed = c;
    changed.command = take(parseCompileCommand({"g++", "main.cpp", row.flag}, std::filesystem::current_path()));
    CHECK_EQ(changed.command.args.back().classification, Class::harmless);
    changed.argv.push_back(row.flag);
    if (row.definition) changed.probe.macros[row.macro] = Macro{false, {}, false, row.definition};
    else changed.probe.macros.erase(row.macro);
    CHECK(!targetEquivalent(changed, &p, false));
  }
}

TEST(option_model_flat_options_preserve_order_for_local_and_check_compilers) {
  const auto local = take(parseCompileCommand({"g++", "-O0", "-pthread", "-Wno-deprecated", "-Ofast",
      "-pedantic", "-g3", "-fno-rtti", "-DLOCAL", "-U__X", "-std=gnu++11", "-Iinc", "main.cpp"},
      std::filesystem::current_path()));
  Profile p; p.dialect = {"gnu++20", true, 202002}; p.flags = {"-O2", "-Wdeprecated", "-DJUDGE"};
  for (bool check : {false, true}) {
    const auto cmd = take(flatCommand(local, &p, "g++", check));
    std::vector<std::string> flags;
    for (const auto& a : cmd.args) if (a.classification != Class::input)
      flags.insert(flags.end(), a.spelling.begin(), a.spelling.end());
    CHECK_EQ(flags, (std::vector<std::string>{"-O0", "-pthread", "-Wno-deprecated", "-Ofast", "-pedantic",
        "-g3", "-fno-rtti", "-std=gnu++20", "-O2", "-Wdeprecated", "-DJUDGE"}));
    CHECK(cmd.resolutionAllowed());
  }
}

TEST(option_model_local_gcc_predefined_macro_effects) {
  LocalGcc f; f.linuxOnly();
  struct Effect { std::vector<std::string> flags; const char* name; const char* value; };
  for (const auto& row : std::vector<Effect>{
      {{"-O0"}, "__NO_INLINE__", "1"}, {{"-O0"}, "__OPTIMIZE__", nullptr},
      {{"-O1"}, "__OPTIMIZE__", "1"}, {{"-O2"}, "__NO_INLINE__", nullptr},
      {{"-O3"}, "__OPTIMIZE__", "1"}, {{"-Og"}, "__OPTIMIZE__", "1"},
      {{"-Os"}, "__OPTIMIZE_SIZE__", "1"}, {{"-Oz"}, "__OPTIMIZE_SIZE__", "1"},
      {{"-Ofast"}, "__FAST_MATH__", "1"}, {{"-Ofast"}, "__FINITE_MATH_ONLY__", "1"},
      {{"-Ofast"}, "__NO_MATH_ERRNO__", "1"}, {{"-Ofast"}, "__GCC_IEC_559", "0"},
      {{"-Ofast"}, "__GCC_IEC_559_COMPLEX", "0"}, {{"-pthread"}, "_REENTRANT", "1"},
      {{"-Wno-deprecated"}, "__DEPRECATED", nullptr}, {{"-Wdeprecated"}, "__DEPRECATED", "1"},
      {{"-Wvla"}, "__cpp_runtime_arrays", nullptr}, {{"-Wno-vla"}, "__cpp_runtime_arrays", "198712L"},
      {{"-Werror=vla"}, "__cpp_runtime_arrays", nullptr}, {{"-pedantic"}, "__cpp_runtime_arrays", nullptr},
      {{"-Wpedantic"}, "__cpp_runtime_arrays", nullptr}, {{"-Werror=pedantic"}, "__cpp_runtime_arrays", nullptr},
      {{"-pedantic-errors"}, "__cpp_runtime_arrays", nullptr},
      {{"-pedantic", "-Wno-vla"}, "__cpp_runtime_arrays", "198712L"},
      {{"-fno-exceptions"}, "__EXCEPTIONS", nullptr}, {{"-fno-exceptions"}, "__cpp_exceptions", nullptr},
      {{"-fno-rtti"}, "__GXX_RTTI", nullptr}, {{"-fno-rtti"}, "__cpp_rtti", nullptr},
      {{"-fshort-wchar"}, "__SIZEOF_WCHAR_T__", "2"},
      {{"-fsanitize=address"}, "__SANITIZE_ADDRESS__", "1"}}) {
    const auto& macros = f.configured(row.flags).probe.macros;
    if (row.value) {
      CHECK(macros.contains(row.name));
      if (macros.contains(row.name)) CHECK_EQ(macros.at(row.name).replacement, row.value);
    } else CHECK(!macros.contains(row.name));
  }
  CHECK(!f.command({"-Oz"}).resolutionAllowed()); // Not in the accepted optimization spellings.
  CHECK(f.configured({"-fopenmp"}).probe.macros.contains("_OPENMP"));
  if (f.configured({}).probe.identity.major >= 15)
    CHECK(f.configured({"-fmodules"}).probe.macros.contains("__cpp_modules"));
  const auto& baseline = f.configured({}).probe.macros;
  for (const auto* flag : {"-g", "-g0", "-g1", "-g2", "-g3", "-w", "-static", "-static-libgcc",
                         "-static-libstdc++", "-s", "-pipe", "-lm", "-lpthread", "-lstdc++",
                         "-Werror=deprecated", "-Wno-deprecated-declarations"})
    CHECK_EQ(f.configured({flag}).probe.macros, baseline);
  // Before C++14, -pedantic also removes the binary-literal extension feature macro.
  auto old = f.command({"-pedantic"}, "gnu++11");
  CHECK(!f.probe(old).probe.macros.contains("__cpp_binary_literals"));
}

TEST(option_model_without_profile_keeps_option_macros_unknown) {
  LocalGcc f;
  auto cmd = f.command({"-O2", "-pthread", "-Wno-deprecated", "-Ofast", "-pedantic", "-fno-rtti"});
  std::string text;
  for (const auto* name : {"__OPTIMIZE__", "__OPTIMIZE_SIZE__", "__NO_INLINE__", "_REENTRANT",
                         "__DEPRECATED", "__FAST_MATH__", "__FINITE_MATH_ONLY__", "__cpp_runtime_arrays", "__GXX_RTTI"})
    text += "#ifdef " + std::string(name) + "\nint present;\n#endif\n";
  text += "#define OWN_GUARD\n#ifdef OWN_GUARD\nint own;\n#endif\n#ifdef __cplusplus\nint dialect;\n#endif\n";
  auto raw = source(text);
  auto resolution = take(resolveConditions(raw, cmd, *f.evaluator));
  CHECK(resolution.disabledReasons.empty());
  std::vector<Decision> conditions;
  for (const auto& d : resolution.decisions) if (d.directive == "ifdef") conditions.push_back(d.decision);
  CHECK_EQ(conditions.size(), 11u);
  for (std::size_t i = 0; i < 9; ++i) CHECK_EQ(conditions.at(i), Decision::unknown);
  CHECK_EQ(conditions.at(9), Decision::trueValue);
  CHECK_EQ(conditions.at(10), Decision::trueValue);
  CHECK(take(satisfies(f.probe(cmd), raw, cmd, resolution, *f.evaluator)));
}

TEST(option_model_profile_decisions_satisfaction_and_target_matrix) {
  LocalGcc f; f.linuxOnly();
  struct Case {
    std::vector<std::string> local, judge;
    const char* expression;
    bool judgeTrue, satisfied;
  };
  const std::vector<Case> cases{
    {{"-O0"}, {}, "defined(__NO_INLINE__)", true, true},
    {{"-O0"}, {"-O2"}, "defined(__OPTIMIZE__)", true, true},
    {{"-O2"}, {}, "defined(__OPTIMIZE__)", false, false},
    {{"-O2"}, {"-O0"}, "defined(__NO_INLINE__)", true, true},
    {{"-Os"}, {}, "defined(__OPTIMIZE_SIZE__)", false, false},
    {{}, {"-Os"}, "defined(__OPTIMIZE_SIZE__)", true, true},
    {{"-pthread"}, {}, "defined(_REENTRANT)", false, false},
    {{}, {"-pthread"}, "defined(_REENTRANT)", true, true},
    {{"-Wno-deprecated"}, {}, "defined(__DEPRECATED)", true, false},
    {{}, {"-Wno-deprecated"}, "defined(__DEPRECATED)", false, true},
    {{"-Wno-deprecated"}, {"-Wdeprecated"}, "defined(__DEPRECATED)", true, true},
    {{"-Wvla"}, {}, "defined(__cpp_runtime_arrays)", true, false},
    {{"-Werror=vla"}, {}, "defined(__cpp_runtime_arrays)", true, false},
    {{"-Wpedantic"}, {}, "defined(__cpp_runtime_arrays)", true, false},
    {{"-Werror=pedantic"}, {}, "defined(__cpp_runtime_arrays)", true, false},
    {{"-Wno-vla"}, {"-Wvla"}, "defined(__cpp_runtime_arrays)", false, true},
    {{"-pedantic"}, {}, "defined(__cpp_runtime_arrays)", true, false},
    {{"-pedantic-errors"}, {}, "defined(__cpp_runtime_arrays)", true, false},
    {{"-pedantic", "-Wno-vla"}, {}, "defined(__cpp_runtime_arrays)", true, true},
    {{"-Ofast"}, {}, "__FAST_MATH__", false, false},
    {{}, {"-Ofast"}, "__FAST_MATH__", true, true},
    // GCC's final optimization level also selects its implied math defaults.
    {{"-Ofast"}, {"-O2"}, "__FINITE_MATH_ONLY__", false, true},
    {{"-fno-exceptions"}, {}, "defined(__EXCEPTIONS)", true, false},
    {{}, {"-fno-exceptions"}, "defined(__EXCEPTIONS)", false, true},
    {{"-fno-rtti"}, {}, "defined(__GXX_RTTI)", true, false},
    {{"-fsanitize=address"}, {}, "defined(__SANITIZE_ADDRESS__)", false, false},
  };
  for (const auto& row : cases) {
    auto p = f.profile(row.judge); auto cmd = f.command(row.local);
    auto raw = source(conditional(row.expression));
    ResolveOptions options; options.profile = &p;
    const auto resolution = take(resolveConditions(raw, cmd, *f.evaluator, options));
    CHECK(resolution.disabledReasons.empty());
    CHECK_EQ(resolution.decisions.front().decision, row.judgeTrue ? Decision::trueValue : Decision::falseValue);
    // Both local fallback and explicit check compiler retain these local options.
    for (bool check : {false, true}) {
      auto c = f.probe(take(flatCommand(cmd, &p, f.gcc, check)));
      CHECK_EQ(take(satisfies(c, raw, cmd, resolution, *f.evaluator)), row.satisfied);
      CHECK_EQ(targetEquivalent(c, &p, false), c.probe.macros == p.predefined);
      if (!row.satisfied) CHECK(!targetEquivalent(c, &p, false));
    }
    // Irrelevant macro differences may satisfy this condition, but still deny target.
    auto unrelated = source("#define OWN\n#ifdef OWN\nint own;\n#endif\n");
    auto ownResolution = take(resolveConditions(unrelated, cmd, *f.evaluator, options));
    auto c = f.probe(take(flatCommand(cmd, &p, f.gcc, false)));
    CHECK(take(satisfies(c, unrelated, cmd, ownResolution, *f.evaluator)));
    if (c.probe.macros != p.predefined) CHECK(!targetEquivalent(c, &p, false));
  }
}

TEST(option_model_pedantic_extension_macros_in_older_dialects) {
  LocalGcc f;
  for (const auto* dialect : {"gnu++11", "c++11"}) for (bool judgePedantic : {false, true}) {
    std::vector<std::string> judgeFlags;
    if (judgePedantic) judgeFlags.push_back("-pedantic");
    auto p = f.probe(f.command(judgeFlags, dialect)).facts;
    if (!p.reproduced) throw test::Skip{"local GCC lacks a complete special-builtin registry"};
    p.flags = judgeFlags;
    const auto cmd = f.command({"-pedantic"}, dialect);
    const auto c = f.probe(take(flatCommand(cmd, &p, f.gcc, false)));
    for (const auto* name : {"__cpp_binary_literals", "__cpp_hex_float"}) {
      // GNU dialects enable extended numbers even under -pedantic.
      if (std::string_view(dialect) == "gnu++11" && std::string_view(name) == "__cpp_hex_float") continue;
      const auto raw = source(conditional("defined(" + std::string(name) + ")"));
      ResolveOptions options; options.profile = &p;
      const auto resolution = take(resolveConditions(raw, cmd, *f.evaluator, options));
      CHECK_EQ(resolution.decisions.front().decision, judgePedantic ? Decision::falseValue : Decision::trueValue);
      CHECK_EQ(take(satisfies(c, raw, cmd, resolution, *f.evaluator)), judgePedantic);
      CHECK_EQ(targetEquivalent(c, &p, false), judgePedantic);
    }
  }
}

TEST(option_model_approximate_profile_withholds_build_macros_but_checks_feature_facts) {
  LocalGcc f; auto p = f.profile({}); p.reproduced = false;
  ResolveOptions options; options.profile = &p;
  const auto cmd = f.command({"-O2", "-pthread", "-Wno-deprecated", "-Wvla"});
  for (const auto* name : {"__OPTIMIZE__", "_REENTRANT", "__DEPRECATED", "__FAST_MATH__"}) {
    const auto raw = source(conditional("defined(" + std::string(name) + ")"));
    const auto resolution = take(resolveConditions(raw, cmd, *f.evaluator, options));
    CHECK_EQ(resolution.decisions.front().decision, Decision::unknown);
  }
  const auto raw = source(conditional("defined(__cpp_runtime_arrays)"));
  const auto resolution = take(resolveConditions(raw, cmd, *f.evaluator, options));
  CHECK_EQ(resolution.decisions.front().decision, Decision::trueValue);
  const auto c = f.probe(take(flatCommand(cmd, &p, f.gcc, false)));
  CHECK(!take(satisfies(c, raw, cmd, resolution, *f.evaluator)));
  CHECK(!targetEquivalent(c, &p, false));
}

TEST(option_model_optimization_facts_are_invalidated_only_after_pragma) {
  LocalGcc f; auto p = f.profile({"-O2"});
  auto raw = source("#ifdef __OPTIMIZE__\nint before;\n#endif\n#pragma GCC optimize(\"O0\")\n"
                    "#ifdef __OPTIMIZE__\nint after;\n#endif\n");
  ResolveOptions options; options.profile = &p;
  auto resolution = take(resolveConditions(raw, f.command({"-O0"}), *f.evaluator, options));
  std::vector<Decision> conditions;
  for (const auto& d : resolution.decisions) if (d.directive == "ifdef") conditions.push_back(d.decision);
  CHECK_EQ(conditions, (std::vector<Decision>{Decision::trueValue, Decision::unknown}));
}

TEST(option_model_pipeline_preserves_judge_behavior_or_falls_back) {
  LocalGcc f; f.linuxOnly();
  struct Case { std::vector<std::string> local, judge; const char* expression; bool flat; bool target = true; };
  for (const auto& row : std::vector<Case>{
      {{"-O0"}, {}, "defined(__NO_INLINE__)", true},
      {{"-O0"}, {"-O2"}, "defined(__OPTIMIZE__)", true},
      {{"-O2"}, {}, "defined(__OPTIMIZE__)", false},
      {{"-pthread"}, {}, "defined(_REENTRANT)", false},
      {{"-pthread"}, {}, "defined(__DEPRECATED)", true, false},
      {{}, {"-pthread"}, "defined(_REENTRANT)", true},
      {{"-Wno-deprecated"}, {}, "defined(__DEPRECATED)", false},
      {{}, {"-Wno-deprecated"}, "defined(__DEPRECATED)", true},
      {{"-Werror=vla"}, {}, "defined(__cpp_runtime_arrays)", false},
      {{"-pedantic"}, {}, "defined(__cpp_runtime_arrays)", false},
      {{"-Ofast"}, {}, "__FAST_MATH__", false},
      {{}, {"-Ofast"}, "__FAST_MATH__", true},
      {{"-Ofast"}, {"-O2"}, "__FINITE_MATH_ONLY__", true}}) {
    auto p = f.profile(row.judge); auto cmd = f.command(row.local);
    f.temp.write("main.cpp", conditional(row.expression));
    DistillOptions options; options.profile = &p; options.temporaryRoot = f.temp.path;
    const auto result = take(distillNoPrune(cmd, options, *f.evaluator));
    // A disagreeing local configuration no longer forces raw: the conditional
    // is demoted and kept for the judge's compiler (cross-host demotion).
    CHECK_EQ(result.exitCode, 0); CHECK_EQ(result.artifact, "flat");
    if (row.flat) CHECK_EQ(result.verification, row.target ? "target" : "local");
    else {
      CHECK(result.diagnostic.find("flat: demoted main.cpp:") != std::string::npos);
      CHECK(result.diagnostic.find("no configuration satisfies resolution") == std::string::npos);
      CHECK(result.bytes.find(row.expression) != std::string::npos);
    }
    // Independently compare the delivered bytes to the original under JUST the
    // judge flags, not the possibly incompatible mixed checking configuration.
    f.temp.write("delivered.cpp", result.bytes);
    auto preprocess = [&](const char* file) {
      ProcessSpec spec; spec.cwd = f.temp.path;
      spec.argv = {pathToUtf8(f.gcc), "-std=" + p.dialect.name, "-E", file};
      spec.argv.insert(spec.argv.end(), p.flags.begin(), p.flags.end());
      auto run = take(runProcess(spec)); CHECK_EQ(run.exitCode, 0);
      return run.stdoutText;
    };
    const auto original = preprocess("main.cpp"), delivered = preprocess("delivered.cpp");
    CHECK(comparePreprocessed(original, delivered, [](std::string_view file) {
      return file == "delivered.cpp" ? "main.cpp" : std::string(file);
    }).ok());
  }
}
