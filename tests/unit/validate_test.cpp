// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.h"
#include <algorithm>
#include "core/validate/preprocess.h"
#include "core/command/options.h"
#include "core/prune/extract.h"
using namespace distill;
TEST(validate_preprocess_markers_flags_blanks_escapes) {
  CHECK(comparePreprocessed("# 3 \"a\\\\b\\\"c\" 1 3 4\n\nint x;\n", "# 4 \"a\\\\b\\\"c\" 2\nint x;\n").ok());
  CHECK(comparePreprocessed("# 3 \"a.cpp\" 1\r\n\r\nint x;\r\n", "# 4 \"a.cpp\" 2\nint x;\n").ok());
  SourceLocation at;
  CHECK(!parseLineMarker("# 9999999999999999999999999 \"a\"", at).ok());
  CHECK(!parseLineMarker("# 1 \"a\" 8", at).ok());
  CHECK(!parseLineMarker("# 1 \"a", at).ok());
  CHECK(!parseLineMarker("# 1 a", at).ok());
  // [cpp.line]: without a file name the presumed file is kept.
  bool named = true; at = {"kept", 1};
  CHECK(parseLineMarker("#line 7", at, nullptr, &named).value());
  CHECK(!named); CHECK_EQ(at.file, std::string("kept")); CHECK_EQ(at.line, 7u);
}
TEST(validate_short_markers_only_within_a_segment) {
  PresumedFile p;
  CHECK_EQ(p.marker({"a", 1}), std::string("#line 1 \"a\"\n")); // The first one names its file.
  CHECK_EQ(p.marker({"a", 5}), std::string("#line 5\n"));
  p.directive("define"); p.directive("undef"); p.directive("pragma");
  CHECK_EQ(p.marker({"a", 9}), std::string("#line 9\n"));
  CHECK_EQ(p.marker({"q\"\\", 1}), std::string("#line 1 \"q\\\"\\\\\"\n"));
  for (const char* d : {"if", "ifdef", "ifndef", "elif", "elifdef", "elifndef", "else", "endif", "include", "include_next", "line", "embed"}) {
    p.directive(d);
    CHECK_EQ(p.marker({"q\"\\", 2}), std::string("#line 2 \"q\\\"\\\\\"\n"));
  }
  PresumedFile q; SourceLocation at;
  CHECK(!q.parse("#line 3", at).ok()); // No preceding marker.
  CHECK(q.parse("#line 2 \"x\"", at).value());
  CHECK(q.parse("#line 4", at).value()); CHECK_EQ(at.file, std::string("x")); CHECK_EQ(at.line, 4u);
  q.directive("define");
  CHECK(q.parse("#line 6", at).value()); CHECK_EQ(at.file, std::string("x"));
  q.directive("endif"); // A previous branch may have changed the presumed file.
  CHECK(!q.parse("#line 8", at).ok());
  CHECK(!q.parse("int x;", at).value());
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
TEST(validate_unmarked_preprocess_same_tokens_and_system_lines) {
  const std::string head = "# 1 \"/usr/include/s.h\" 1 3\nint s;\n";
  const auto marked = head + "# 7 \"main.cpp\"\n#pragma once\nint x;\n";
  CHECK(compareUnmarkedPreprocessed(marked, head + "# 2 \"u.cpp\"\n#pragma once\nint x;\n").ok());
  for (const auto& unmarked : {std::string("# 2 \"/usr/include/s.h\" 1 3\nint s;\n# 2 \"u.cpp\"\n#pragma once\nint x;\n"), // System line moved.
                               head + "# 2 \"u.cpp\"\n#pragma once int x;\n",  // Boundary lost.
                               head + "# 2 \"u.cpp\"\n#pragma once\nint y;\n", // Token differs.
                               head + "# 2 \"u.cpp\"\n#pragma once\n"}) {      // Token lost.
    auto r = compareUnmarkedPreprocessed(marked, unmarked); CHECK(!r.ok());
    if (!r.ok()) CHECK(r.error().message.starts_with("unmarked preprocess difference at token "));
  }
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
  CHECK_EQ(parseDistillOptions({"--judge", "list"}).value().judge.value(), "list");
  CHECK(parseDistillOptions({"--version"}).value().version);
  CHECK(parseDistillOptions({"-v", "--version"}).value().compile.empty());
  CHECK(!parseDistillOptions({"--version=1"}).ok());
  CHECK_EQ(parseDistillOptions({"g++", "--version"}).value().compile,
           (std::vector<std::string>{"g++", "--version"}));
  CHECK(distillHelp().find("--version") != std::string::npos);
  CHECK(distillVersion().starts_with("distill "));
  CHECK(distillVersion().find(" (commit ") != std::string::npos);
  CHECK(distillVersion().find("; LLVM ") != std::string::npos);
  CHECK(distillVersion().ends_with(")\n"));
  CHECK_EQ(distillVersion(), std::string("distill ") + DISTILL_EXPECTED_VERSION +
      " (commit " + DISTILL_EXPECTED_COMMIT + "; LLVM " + DISTILL_EXPECTED_LLVM_REVISION + ")\n");
  auto defaults = parseDistillOptions({"--keep","first","--keep","second","--strip-comments","main.cpp"});
  CHECK(defaults.ok()); CHECK(!defaults.value().noPrune); CHECK(!defaults.value().mergeOnly); CHECK(defaults.value().stripComments);
  CHECK_EQ(defaults.value().keep,(std::vector<std::string>{"first","second"}));
  CHECK(!defaults.value().noLineMarkers); CHECK(!defaults.value().compactWhitespace);
  const auto compact = parseDistillOptions({"--compact-whitespace","main.cpp"});
  CHECK(compact.ok()); CHECK(compact.value().compactWhitespace); CHECK(compact.value().noLineMarkers);
  CHECK(!parseDistillOptions({"--compact-whitespace","--judge-probe","x"}).ok());
  CHECK(distillHelp().find("--compact-whitespace") != std::string::npos);
  auto lines = parseDistillOptions({"--no-line-markers","main.cpp"});
  CHECK(lines.ok()); CHECK(lines.value().noLineMarkers);
  CHECK(!parseDistillOptions({"--no-line-markers","--judge-probe","x"}).ok());
  CHECK(distillHelp().find("  --no-line-markers  ") != std::string::npos);
  auto samples = parseDistillOptions({"--verify-run","one.in","--verify-run","dir","main.cpp"});
  CHECK(samples.ok()); CHECK_EQ(samples.value().verifyRun,(std::vector<std::string>{"one.in","dir"}));
  CHECK(parseDistillOptions({"main.cpp"}).ok());
  for (const auto& args : std::vector<std::vector<std::string>>{{"--no-prune"}, {"--no-prune", "-o"}, {"--no-prune", "--bogus"}, {"--keep"}, {"--verify-run"}, {"--strip-comments"}, {"--no-line-markers"}, {"--compact-whitespace"}})
    CHECK(!parseDistillOptions(args).ok());
}

TEST(validate_merge_only_options) {
  for (const auto& flags : std::vector<std::vector<std::string>>{
      {}, {"--no-prune"}, {"--strip-comments"}, {"--judge","codeforces"},
      {"--verify-run","sample.in"}, {"--no-prune","--strip-comments","--judge","codeforces","--verify-run","sample.in"}}) {
    auto args = flags; args.insert(args.begin(),"--merge-only"); args.push_back("main.cpp");
    const auto parsed = parseDistillOptions(args); CHECK(parsed.ok());
    if (parsed.ok()) CHECK(parsed.value().mergeOnly);
  }
  for (const auto& flags : std::vector<std::vector<std::string>>{
      {"--no-line-markers"}, {"--compact-whitespace"}, {"--shorten-names"},
      {"--keep","main"}, {"--strict"}, {"--no-assume=instantiation-effects"},
      {"--no-assume","library-spelled-members"}, {"--no-assume=library-macro-values"},
      {"--no-assume=unspelled-functions"}, {"--no-assume=library-spelled-names"},
      {"--no-assume=idempotent-standard-headers"}, {"--no-assume=unobserved-guard-macros"},
      {"--no-assume=instantiation-effects,library-macro-values"}}) {
    for (const bool first : {false,true}) {
      auto args = flags;
      args.insert(first ? args.begin() : args.end(),"--merge-only"); args.push_back("main.cpp");
      const auto parsed = parseDistillOptions(args); CHECK(!parsed.ok());
      if (!parsed.ok()) CHECK_EQ(parsed.error().message,"--merge-only cannot be combined with " + flags.front().substr(0,flags.front().find('=')));
    }
  }
  CHECK(!parseDistillOptions({"--merge-only","--judge-probe","cses"}).ok());
  CHECK(!parseDistillOptions({"--merge-only","--judge-check","cses","output.txt"}).ok());
  CHECK(!parseDistillOptions({"--merge-only"}).ok());
  CHECK(!parseDistillOptions({"--merge-only=yes","main.cpp"}).ok());
  CHECK(distillHelp().find("--merge-only") != std::string::npos);
  CHECK(distillHelp().find("remove inactive #if branches (flat)") != std::string::npos);
}

TEST(validate_merge_only_speed_options) {
  for (const auto& flags : std::vector<std::vector<std::string>>{
      {"--jobs=4"}, {"--jobs","2"}, {"-j","4"}, {"--verify=all","--jobs=4"}}) {
    for (const bool first : {false,true}) {
      auto args = flags;
      args.insert(first ? args.begin() : args.end(),"--merge-only"); args.push_back("main.cpp");
      const auto parsed = parseDistillOptions(args); CHECK(parsed.ok());
      if (parsed.ok()) {
        CHECK(parsed.value().mergeOnly); CHECK(!parsed.value().verifyFinal);
        CHECK_EQ(parsed.value().jobs,flags.front() == "--jobs" ? 2u : 4u);
      }
    }
  }
  for (const auto& flags : std::vector<std::vector<std::string>>{
      {"--verify=final"}, {"--verify","final"}, {"--jobs=4","--verify=final"}}) {
    for (const bool first : {false,true}) {
      auto args = flags;
      args.insert(first ? args.begin() : args.end(),"--merge-only"); args.push_back("main.cpp");
      const auto parsed = parseDistillOptions(args); CHECK(!parsed.ok());
      if (!parsed.ok()) CHECK_EQ(parsed.error().message,"--merge-only cannot be combined with --verify=final");
    }
  }
}

#include "core/pipeline/pipeline.h"
#include "evaluate.h"
#include "profile.h"
TEST(validate_no_assume_names) {
  CHECK(parseDistillOptions({"main.cpp"}).value().assumeInstantiationEffects);
  for (const auto& args : std::vector<std::vector<std::string>>{
      {"--no-assume=instantiation-effects","main.cpp"},
      {"--no-assume","instantiation-effects","main.cpp"},
      {"--no-assume=instantiation-effects","--no-assume=instantiation-effects","main.cpp"},
      {"--no-assume=instantiation-effects,instantiation-effects","main.cpp"}}) {
    auto r = parseDistillOptions(args); CHECK(r.ok()); CHECK(!r.value().assumeInstantiationEffects);
    CHECK(!r.value().noPrune);
  }
  CHECK(parseDistillOptions({"main.cpp"}).value().assumeLibrarySpelledMembers);
  for (const auto& args : std::vector<std::vector<std::string>>{
      {"--no-assume=library-spelled-members","main.cpp"},
      {"--no-assume","library-spelled-members","main.cpp"},
      {"--no-assume=library-spelled-members,library-spelled-members","main.cpp"}}) {
    const auto r = parseDistillOptions(args); CHECK(r.ok());
    CHECK(!r.value().assumeLibrarySpelledMembers); CHECK(r.value().assumeInstantiationEffects); CHECK(!r.value().noPrune);
  }
  const auto both = parseDistillOptions({"--no-assume=instantiation-effects,library-spelled-members","main.cpp"});
  CHECK(both.ok()); CHECK(!both.value().assumeLibrarySpelledMembers); CHECK(!both.value().assumeInstantiationEffects);
  CHECK(parseDistillOptions({"main.cpp"}).value().assumeLibraryMacroValues);
  for (const auto& args : std::vector<std::vector<std::string>>{
      {"--no-assume=library-macro-values","main.cpp"},
      {"--no-assume","library-macro-values","main.cpp"},
      {"--no-assume=library-macro-values","--no-assume=library-macro-values","main.cpp"},
      {"--no-assume=library-macro-values,library-macro-values","main.cpp"}}) {
    const auto r = parseDistillOptions(args); CHECK(r.ok());
    CHECK(!r.value().assumeLibraryMacroValues); CHECK(r.value().assumeInstantiationEffects);
    CHECK(r.value().assumeLibrarySpelledMembers); CHECK(!r.value().noPrune);
  }
  const auto all = parseDistillOptions({"--no-assume=instantiation-effects,library-macro-values,library-spelled-members","main.cpp"});
  CHECK(all.ok()); CHECK(!all.value().assumeInstantiationEffects);
  CHECK(!all.value().assumeLibrarySpelledMembers); CHECK(!all.value().assumeLibraryMacroValues);
  CHECK(!parseDistillOptions({"--no-assume=library-macro-values","--judge-probe","cses"}).ok());
  CHECK(!parseDistillOptions({"--no-assume=library-macro-values","--judge-check","cses","output.txt"}).ok());
  CHECK(!parseDistillOptions({"--no-assume=library-spelled-members","--judge-probe","cses"}).ok());
  for (const auto* name : {"", "A11", "A12", "A9", "A7", "unknown", "library-macro-values,", ",library-macro-values", "instantiation-effects,", ",instantiation-effects",
                           "instantiation-effects,unknown", "unknown,instantiation-effects"}) {
    const auto r = parseDistillOptions({std::string("--no-assume=")+name,"main.cpp"});
    CHECK(!r.ok()); CHECK(r.error().message.find("switchable names: instantiation-effects, library-spelled-members, library-macro-values") != std::string::npos);
  }
  CHECK(!parseDistillOptions({"--no-assume"}).ok());
  CHECK(!parseDistillOptions({"--no-assume=instantiation-effects","--judge-probe","cses"}).ok());
  CHECK(distillHelp().find("--no-assume=<name>") != std::string::npos);
  CHECK(distillHelp().find("library-macro-values (A9 standard macros)") != std::string::npos);
}

TEST(validate_capture_profile_crlf_compiler_output) {
  const auto gcc = testing::testGcc(); if (!gcc) throw test::Skip{"GCC unavailable"};
  test::TempDirectory temp;
  auto crlf = [](std::string_view text) {
    std::string result;
    for (std::size_t i = 0; i < text.size(); ++i) {
      if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') continue;
      if (text[i] == '\n') result += '\r';
      result += text[i];
    }
    return result;
  };
  auto captured = testing::captureProfile(*gcc, temp.path, [&](const ProcessSpec& spec) -> Result<ProcessResult> {
    auto r = runProcess(spec);
    if (r.ok()) {
      // Exercise every captured stream, including -dM, -dD, -v and --version.
      if (spec.argv.back() == "--version") r.value().stdoutText = "g++ (fixture)\nCopyright fixture\n";
      r.value().stdoutText = crlf(r.value().stdoutText);
      r.value().stderrText = crlf(r.value().stderrText);
    }
    return r;
  });
  if (!captured.ok()) throw std::runtime_error(captured.error().message);
  const auto& p = captured.value();
  CHECK_EQ(p.snapshotMetadata.versionOutput, "g++ (fixture)\\nCopyright fixture\\n");
  CHECK_EQ(p.predefined.at("__GNUC__").replacement, std::to_string(p.compiler.major));
  CHECK_EQ(p.predefined.at("DISTILL_FIXTURE_JUDGE").replacement, "1");
  const auto snapshot = test::read(temp.path / "fixture-predefined.txt");
  CHECK(snapshot.find('\r') == snapshot.npos);
  // CRLF record separators remain valid; an embedded bare CR remains invalid.
  temp.write("fixture-predefined.txt", crlf(snapshot));
  auto loaded = loadProfile(p.provenance.file);
  CHECK(loaded.ok());
  if (loaded.ok()) {
    CHECK_EQ(loaded.value().snapshotMetadata.versionOutput, p.snapshotMetadata.versionOutput);
    CHECK_EQ(loaded.value().predefined, p.predefined);
  }
  auto malformed = snapshot;
  malformed.insert(malformed.find("\\nCopyright"), 1, '\r');
  temp.write("fixture-predefined.txt", malformed);
  auto rejected = loadProfile(p.provenance.file);
  CHECK(!rejected.ok());
  if (!rejected.ok()) CHECK(rejected.error().message.find("fixture-predefined.txt:4: invalid UTF-8 or control character") != std::string::npos);
}
TEST(validate_configuration_exact_flags_specials_and_target) {
  const auto gcc = testing::testGcc(); if (!gcc) throw test::Skip{"GCC unavailable"};
  test::TempDirectory temp;
  auto captured = testing::captureProfile(*gcc, temp.path);
  if (!captured.ok()) throw std::runtime_error(captured.error().message);
  auto& p = captured.value();
  auto command = parseCompileCommand({pathToUtf8(*gcc), "main.cpp", "-std=gnu++20", "-O2", "-pthread", "-DA=1", "-UA", "-DA=", "-U__has_include", "-DDISTILL_PRESENT_0=0"}, temp.path).value();
  ProbeOptions po; po.temporaryRoot = temp.path;
  bool sawPthreadMacroProbe = false;
  po.runner = [&](const ProcessSpec& spec) -> Result<ProcessResult> {
    if (std::find(spec.argv.begin(), spec.argv.end(), "-dM") != spec.argv.end()) {
      // MinGW accepts -pthread without defining _REENTRANT; inspect the actual invocation.
      CHECK(std::find(spec.argv.begin(), spec.argv.end(), "-pthread") != spec.argv.end());
      sawPthreadMacroProbe = true;
    }
    return runProcess(spec);
  };
  auto c = probeConfiguration(command, "local", po);
  if (!c.ok()) throw std::runtime_error(c.error().message);
  CHECK(sawPthreadMacroProbe);
  CHECK_EQ(c.value().facts.dialect.name, "gnu++20");
  CHECK_EQ(c.value().facts.flags, (std::vector<std::string>{"-O2", "-pthread", "-DA=1", "-UA", "-DA=", "-U__has_include", "-DDISTILL_PRESENT_0=0"}));
  po.runner = runProcess;
  CHECK(c.value().probe.macros.contains("__OPTIMIZE__"));
  CHECK_EQ(c.value().facts.initialState("A").definition->replacement, "");
  CHECK_EQ(c.value().facts.initialState("__has_include").state, MacroState::undefined);
  CHECK(!c.value().probe.special.at("__has_include"));
  CHECK(c.value().probe.special.at("__LINE__"));
  CHECK(!targetEquivalent(c.value(), &p, false)); // -O2 adds __OPTIMIZE__ beyond the -O0 fixture.
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
TEST(validate_profile_link_options_use_command_pruning_policy) {
  auto loaded = lookupProfile("qoj"); CHECK(loaded.ok());
  auto p = loaded.value();
  const auto cwd = std::filesystem::current_path();
  const auto local = parseCompileCommand({"g++", "main.cpp"}, cwd).value();
  ResolveResult flat; flat.text = "int main(){}\n";
  for (const auto* flag : {"-lm", "-lpthread", "-lstdc++", "-static", "-static-libstdc++", "-static-libgcc", "-s",
                           "-lssl", "-Llocal", "-Wl,--stack=268435456"}) {
    p.link = {flag};
    Configuration c; c.command = flatCommand(local, &p, "g++", true).value();
    c.probe.triple = p.triple; c.probe.dialect = p.dialect;
    const auto argv = c.command.buildArgv("flat.cpp", "program", true, {});
    CHECK_EQ(std::count(argv.begin(), argv.end(), flag), 1);
    const auto model = preparePruning(c, {}, flat, &p).value();
    const bool external = std::string_view(flag) == "-lssl" || std::string_view(flag) == "-Llocal" ||
        std::string_view(flag).starts_with("-Wl,");
    CHECK_EQ(c.command.pruningAllowed().allowed, !external);
    if (external) {
      CHECK_EQ(model.disabledReasons, (std::vector<std::string>{"external library option: " + std::string(flag),
          "untranslated semantic option: " + std::string(flag)}));
    } else {
      CHECK(model.disabledReasons.empty());
      CHECK(computeRetention(model).value().disabledReasons.empty());
      const auto direct = parseCompileCommand({"g++", "main.cpp", flag}, cwd).value();
      CHECK_EQ(c.command.args.back().classification, direct.args.back().classification);
    }
    // The local fallback omits profile link options, regardless of classification.
    const auto fallback = flatCommand(local, &p, "g++", false).value().buildArgv("flat.cpp", "program", true, {});
    CHECK(std::find(fallback.begin(), fallback.end(), flag) == fallback.end());
  }
}
TEST(validate_atcoder_flat_pruning_remaining_reasons_and_link_provenance) {
  auto loaded = lookupProfile("atcoder-gcc15-gnupp23"); CHECK(loaded.ok());
  const auto& p = loaded.value();
  const std::vector<std::string> semantic{
    "untranslated semantic option: -fconstexpr-depth=1024",
    "untranslated semantic option: -fconstexpr-loop-limit=524288",
    "untranslated semantic option: -fconstexpr-ops-limit=2097152",
    "untranslated semantic option: -fmodules",
    "untranslated semantic option: -ftrivial-auto-var-init=zero",
    "untranslated semantic option: -march=native",
    "untranslated semantic option: -fopenmp"};
  ResolveResult flat; flat.text = "int main(){}\n";
  for (const bool libraries : {false, true}) for (const bool check : {false, true}) {
    std::vector<std::string> argv{"g++", "-isystem", "sys", "-std=c++20", "-DLOCAL", "main.cpp"};
    if (libraries) argv.insert(argv.end(), {"-L", "local-lib", "-llocal"});
    const auto local = parseCompileCommand(argv, std::filesystem::current_path()).value();
    Configuration c; c.command = flatCommand(local, &p, "g++", check).value();
    c.probe.triple = p.triple; c.probe.dialect = p.dialect;
    CHECK(c.command.resolutionAllowed());
    CHECK_EQ(std::count_if(c.command.args.begin(), c.command.args.end(), [](const auto& a) {
      return a.profilePreprocessingInert;
    }), static_cast<std::ptrdiff_t>(p.inert.size()));
    const auto build = c.command.buildArgv("flat.cpp", "program", true, {});
    auto next = build.begin();
    for (const auto& flag : p.flags) {
      next = std::find(next, build.end(), flag); CHECK(next != build.end());
      if (next != build.end()) ++next;
    }
    if (check) for (const auto& flag : p.link) {
      next = std::find(next, build.end(), flag); CHECK(next != build.end());
      if (next != build.end()) ++next;
    }
    CHECK(std::find(build.begin(), build.end(), "-DLOCAL") == build.end());
    CHECK_EQ(std::find(build.begin(), build.end(), "-llocal") != build.end(), libraries && !check);
    std::vector<std::string> expected;
    const auto links = check ? p.link : libraries ? std::vector<std::string>{"-L", "-llocal"} : std::vector<std::string>{};
    for (const auto& flag : links) expected.push_back("external library option: " + flag);
    if (!check && libraries) expected.insert(expected.end(), {"untranslated semantic option: -L local-lib", "untranslated semantic option: -llocal"});
    expected.insert(expected.end(), semantic.begin(), semantic.end());
    if (check) for (const auto& flag : links) expected.push_back("untranslated semantic option: " + flag);
    const auto model = preparePruning(c, {}, flat, &p); CHECK(model.ok());
    CHECK_EQ(model.value().disabledReasons, expected);
    CHECK_EQ(computeRetention(model.value()).value().disabledReasons, expected);
  }
}

TEST(validate_unspelled_functions_option) {
  CHECK(parseDistillOptions({"main.cpp"}).value().assumeUnspelledFunctions);
  for (const auto& args : std::vector<std::vector<std::string>>{
      {"--no-assume=unspelled-functions","main.cpp"},
      {"--no-assume","unspelled-functions","main.cpp"},
      {"--no-assume=unspelled-functions,unspelled-functions","main.cpp"},
      {"--no-assume=unspelled-functions","--no-assume=unspelled-functions","main.cpp"}}) {
    const auto r = parseDistillOptions(args); CHECK(r.ok());
    CHECK(!r.value().assumeUnspelledFunctions); CHECK(r.value().assumeInstantiationEffects);
    CHECK(r.value().assumeLibrarySpelledMembers); CHECK(r.value().assumeLibraryMacroValues);
    CHECK(!r.value().noPrune);
  }
  const auto all = parseDistillOptions({"--no-assume=unspelled-functions,instantiation-effects,library-spelled-members,library-macro-values","main.cpp"});
  CHECK(all.ok()); CHECK(!all.value().assumeUnspelledFunctions); CHECK(!all.value().assumeInstantiationEffects);
  CHECK(!all.value().assumeLibrarySpelledMembers); CHECK(!all.value().assumeLibraryMacroValues);
  CHECK(!parseDistillOptions({"--no-assume=unspelled-functions","--judge-probe","cses"}).ok());
  CHECK(!parseDistillOptions({"--no-assume=unspelled-functions","--judge-check","cses","out.txt"}).ok());
  CHECK(distillHelp().find("unspelled-functions (D3f)") != std::string::npos);
}

TEST(validate_pp_trimming_options) {
  for (const auto* name : {"idempotent-standard-headers","unobserved-guard-macros"}) {
    const auto flags = std::string(name) + ',' + name;
    for (const auto& args : {std::vector<std::string>{"--no-assume="+flags,"main.cpp"},
                            std::vector<std::string>{"--no-assume",name,"main.cpp"}}) {
      const auto r = parseDistillOptions(args); CHECK(r.ok());
      CHECK(r.value().assumeIdempotentStandardHeaders == (std::string_view(name) != "idempotent-standard-headers"));
      CHECK(r.value().assumeUnobservedGuardMacros == (std::string_view(name) != "unobserved-guard-macros"));
    }
    CHECK(!parseDistillOptions({"--no-assume="+std::string(name),"--judge-probe","cses"}).ok());
    CHECK(distillHelp().find(name) != std::string::npos);
  }
}

TEST(validate_speed_options_and_compiler_boundary) {
  const auto defaults = parseDistillOptions({"main.cpp"});
  CHECK_EQ(defaults.value().jobs,1u); CHECK(!defaults.value().verifyFinal);
  for (const auto& args : std::vector<std::vector<std::string>>{
      {"--jobs=4","main.cpp"},
      {"--jobs","4","main.cpp"},
      {"-j","4","main.cpp"}}) {
    const auto r = parseDistillOptions(args); CHECK(r.ok());
    CHECK_EQ(r.value().jobs,4u);
  }
  CHECK(!parseDistillOptions({"-j4","main.cpp"}).ok());
  const auto compiler = parseDistillOptions({"g++","--jobs=4","-j","4","main.cpp"});
  CHECK_EQ(compiler.value().jobs,1u);
  CHECK_EQ(compiler.value().compile,(std::vector<std::string>{"g++","--jobs=4","-j","4","main.cpp"}));
  for (const auto& args : std::vector<std::vector<std::string>>{
      {"--jobs"}, {"--jobs=0","main.cpp"}, {"--jobs=-1","main.cpp"},
      {"--jobs=+2","main.cpp"}, {"--jobs=1.5","main.cpp"}, {"--jobs=4x","main.cpp"},
      {"--jobs=","main.cpp"}, {"--jobs=99999999999999999999","main.cpp"},
      {"--jobs=4","--judge-probe","cses"}, {"--jobs=1","--judge-probe","cses"},
      {"--jobs=4","--judge-check","cses","result.txt"}})
    CHECK(!parseDistillOptions(args).ok());
  CHECK(distillHelp().find("--jobs=N") != std::string::npos);
  CHECK(distillHelp().find("--baseline") == std::string::npos);
  for (const auto* flag : {"--baseline", "--baseline=build", "--baseline=skip"}) {
    const auto removed = parseDistillOptions({flag,"main.cpp"});
    CHECK(!removed.ok()); CHECK(removed.error().message.starts_with("unknown distill option: "));
  }
}

TEST(validate_verify_options_and_compiler_boundary) {
  for (const auto& args : std::vector<std::vector<std::string>>{
      {"--verify=final","main.cpp"}, {"--verify","final","main.cpp"},
      {"--verify=final","--jobs=4","--shorten-names","main.cpp"},
      {"--verify=final","--verify-run","sample.in","main.cpp"}}) {
    const auto r = parseDistillOptions(args); CHECK(r.ok()); CHECK(r.value().verifyFinal);
  }
  CHECK(!parseDistillOptions({"--verify=all","main.cpp"}).value().verifyFinal);
  CHECK(!parseDistillOptions({"--verify=final","--verify=all","main.cpp"}).value().verifyFinal);
  const auto compiler = parseDistillOptions({"g++","--verify=final","main.cpp"});
  CHECK(!compiler.value().verifyFinal);
  CHECK_EQ(compiler.value().compile,(std::vector<std::string>{"g++","--verify=final","main.cpp"}));
  for (const auto& args : std::vector<std::vector<std::string>>{
      {"--verify"}, {"--verify=","main.cpp"}, {"--verify=none","main.cpp"},
      {"--verify=Final","main.cpp"}, {"--verify","--strict","main.cpp"},
      {"--verify=all","--judge-probe","cses"}, {"--verify=final","--judge-check","cses","result.txt"}})
    CHECK(!parseDistillOptions(args).ok());
  CHECK(distillHelp().find("--verify=all|final") != std::string::npos);
}
