// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.h"
#include <algorithm>
#include "core/validate/preprocess.h"
#include "core/command/options.h"
using namespace distill;
TEST(validate_preprocess_markers_flags_blanks_escapes) {
  CHECK(comparePreprocessed("# 3 \"a\\\\b\\\"c\" 1 3 4\n\nint x;\n", "# 4 \"a\\\\b\\\"c\" 2\nint x;\n").ok());
  SourceLocation at;
  CHECK(!parseLineMarker("# 9999999999999999999999999 \"a\"", at).ok());
  CHECK(!parseLineMarker("# 1 \"a\" 8", at).ok());
  CHECK(!parseLineMarker("# 1 \"a", at).ok());
  CHECK(!parseLineMarker("# 1 a", at).ok());
}
TEST(validate_preprocess_first_difference) {
  const auto a = "# 10 \"a.cpp\"\nx ++ y;\n";
  for (const auto& b : {"# 11 \"a.cpp\"\nx ++ y;\n", "# 10 \"a.cpp\"\nx + + y;\n", "# 10 \"b.cpp\"\nx ++ y;\n"}) {
    auto r = comparePreprocessed(a, b); CHECK(!r.ok());
    if (!r.ok()) { CHECK(r.error().message.find("left ") != std::string::npos); CHECK(r.error().message.find("right ") != std::string::npos); }
  }
}
TEST(validate_preprocess_multiline_tokens_and_fake_markers) {
  const std::string a = "# 1 \"a\"\nR\"(\n# 99 \"fake\"\n)\";\nint x;\n";
  auto r = parsePreprocessed(a); CHECK(r.ok());
  CHECK_EQ(r.value().front().text, "R\"(\n# 99 \"fake\"\n)\"");
  CHECK_EQ(r.value().back().location.line, 4u);
  CHECK(comparePreprocessed(a, a).ok());
}
TEST(validate_preprocess_pragma_boundaries) {
  // Identical token sequence and locations under marker resets; only the
  // boundary between pragma operand lines differs.
  auto a = "# 1 \"a\"\n#pragma one\n# 1 \"a\"\ntwo\n";
  auto b = "# 1 \"a\"\n#pragma one two\n";
  CHECK(!comparePreprocessed(a, b).ok());
  CHECK(!comparePreprocessed("# 1 \"a\"\n#pragma one\n", "# 1 \"a\"\none\n").ok());
}
TEST(validate_preprocess_user_identity_system_spelling) {
  InlineResult raw;
  raw.userFiles = {{"/root/a.h", "a.h", {1,2}, 0, 1}, {"/root/alias.h", "alias.h", {1,2}, 0, 1}};
  CHECK(comparePreprocessed("# 1 \"a.h\"\nx\n", "# 1 \"alias.h\"\nx\n", originalFileMapper(raw)).ok());
  CHECK(!comparePreprocessed("# 1 \"a.h\" 3\nx\n", "# 1 \"alias.h\" 3\nx\n", originalFileMapper(raw)).ok());
  CHECK(!comparePreprocessed("# 1 \"/sys/a.h\"\nx\n", "# 1 \"/sys/alias.h\"\nx\n", originalFileMapper(raw)).ok());
}
TEST(validate_cli_options) {
  auto r = parseDistillOptions({"--no-prune", "-o", "out.cpp", "--strict", "main.cpp"}); CHECK(r.ok());
  CHECK(!r.value().compilerExplicit); CHECK(r.value().strict); CHECK_EQ(r.value().compile.front(), "g++");
  CHECK(parseDistillOptions({"--help"}).value().help);
  for (const auto& args : std::vector<std::vector<std::string>>{{"main.cpp"}, {"--no-prune"}, {"--no-prune", "-o"}, {"--no-prune", "--bogus"}, {"--keep"}, {"--verify-run"}, {"--strip-comments"}})
    CHECK(!parseDistillOptions(args).ok());
}

#include "core/pipeline/pipeline.h"
#include "evaluate.h"
#include "profile.h"
TEST(validate_configuration_exact_flags_specials_and_target) {
  const auto gcc = testing::testGcc(); if (!gcc) throw test::Skip{"GCC unavailable"};
  test::TempDirectory temp;
  auto captured = testing::captureProfile(*gcc, temp.path);
  if (!captured.ok()) throw std::runtime_error(captured.error().message);
  auto& p = captured.value();
  auto command = parseCompileCommand({pathToUtf8(*gcc), "main.cpp", "-std=gnu++20", "-O2", "-pthread", "-DA=1", "-UA", "-DA=", "-U__has_include", "-DDISTILL_PRESENT_0=0"}, temp.path).value();
  ProbeOptions po; po.temporaryRoot = temp.path;
  auto c = probeConfiguration(command, "local", po);
  if (!c.ok()) throw std::runtime_error(c.error().message);
  CHECK(c.value().probe.macros.contains("__OPTIMIZE__"));
  CHECK(c.value().probe.macros.contains("_REENTRANT"));
  CHECK_EQ(c.value().facts.initialState("A").definition->replacement, "");
  CHECK_EQ(c.value().facts.initialState("__has_include").state, MacroState::undefined);
  CHECK(!c.value().probe.special.at("__has_include"));
  CHECK(c.value().probe.special.at("__LINE__"));
  auto flat = flatCommand(command, &p, *gcc, false); CHECK(flat.ok());
  auto matched = probeConfiguration(flat.value(), "local", po); CHECK(matched.ok());
  CHECK(!targetEquivalent(matched.value(), &p, false)); // -pthread/-O2 add macros beyond fixture.
  auto pure = parseCompileCommand({pathToUtf8(*gcc), "main.cpp", "-std=gnu++20"}, temp.path).value();
  auto exact = probeConfiguration(flatCommand(pure, &p, *gcc, false).value(), "local", po); CHECK(exact.ok());
  CHECK_EQ(targetEquivalent(exact.value(), &p, false), p.reproduced);
  CHECK(!targetEquivalent(exact.value(), &p, true));
  if (p.reproduced) {
    auto changed = exact.value(); changed.probe.special["__LINE__"] = false;
    CHECK(!targetEquivalent(changed, &p, false));
    changed = exact.value(); changed.probe.macros.erase("__GNUC__");
    CHECK(!targetEquivalent(changed, &p, false));
  }
}
TEST(validate_pipeline_limits_and_no_implicit_clang) {
  const auto gcc = testing::testGcc(); if (!gcc) throw test::Skip{"GCC unavailable"};
  test::TempDirectory temp; temp.write("main.cpp", "int main(){}\n");
  auto e = testing::testEvaluator(temp.path);
  auto cmd = parseCompileCommand({pathToUtf8(*gcc), "main.cpp"}, temp.path).value();
  DistillOptions o; o.maxProcessOutput = 4;
  auto capped = distillNoPrune(cmd, o, *e); CHECK(capped.ok()); CHECK_EQ(capped.value().exitCode, 1);
  CHECK(capped.value().bytes.empty());
  o = {}; o.stepTimeout = std::chrono::milliseconds(0);
  auto timed = distillNoPrune(cmd, o, *e); CHECK(timed.ok()); CHECK_EQ(timed.value().exitCode, 1);
  CHECK(timed.value().diagnostic.find("deadline") != std::string::npos);
  o = {}; o.compilerExplicit = false;
  auto clangProbe = [](const ProcessSpec& s) -> Result<ProcessResult> {
    auto r = runProcess(s);
    if (r.ok() && std::find(s.argv.begin(), s.argv.end(), "-dM") != s.argv.end())
      r.value().stdoutText += "#define __clang__ 1\n#define __clang_major__ 23\n#define __clang_minor__ 1\n#define __clang_patchlevel__ 5\n";
    return r;
  };
  auto implicit = distillNoPrune(cmd, o, *e, clangProbe); CHECK(implicit.ok()); CHECK_EQ(implicit.value().exitCode, 1);
  CHECK(implicit.value().diagnostic.find("name a real GCC") != std::string::npos);
  o.compilerExplicit = true;
  auto explicitClang = distillNoPrune(cmd, o, *e, clangProbe); CHECK(explicitClang.ok()); CHECK_EQ(explicitClang.value().exitCode, 0);
  CHECK(explicitClang.value().diagnostic.find("explicitly named Clang accepted") != std::string::npos);
}
TEST(validate_configuration_preserves_link_order) {
  const auto local = parseCompileCommand({"g++", "-Iinc", "-O2", "main.cpp", "-Llib", "-lfixture"}, std::filesystem::current_path()).value();
  for (const bool check : {false, true}) {
    const auto configured = flatCommand(local, nullptr, "g++", check).value();
    const auto argv = configured.buildArgv("raw.cpp", "program", true, {});
    CHECK(std::find(argv.begin(), argv.end(), "raw.cpp") < std::find(argv.begin(), argv.end(), "-lfixture"));
    CHECK(std::find(argv.begin(), argv.end(), "-O2") < std::find(argv.begin(), argv.end(), "raw.cpp"));
    CHECK(std::find(argv.begin(), argv.end(), "-Iinc") == argv.end());
  }
}
