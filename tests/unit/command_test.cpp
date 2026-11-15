// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.h"
#include "core/command/command.h"
#include <algorithm>

using namespace distill;
namespace {
CompileCommand command(std::vector<std::string> flags) {
  std::vector<std::string> argv{"g++", "main.cpp"};
  argv.insert(argv.end(), flags.begin(), flags.end());
  auto result = parseCompileCommand(argv, std::filesystem::current_path());
  if (!result.ok()) throw std::runtime_error(result.error().message);
  return result.value();
}
void classified(const std::vector<std::string>& words, Class classification) {
  const auto parsed = command(words);
  CHECK_EQ(parsed.args.size(), 2u);
  CHECK_EQ(parsed.args[1].classification, classification);
  CHECK_EQ(parsed.args[1].spelling, words);
}
} // namespace
TEST(command_input_row) {
  for (const auto* name : {"main.cpp", "main.cc", "main.cxx", "main.c++", "main.C"}) {
    auto parsed = parseCompileCommand({"g++", name}, std::filesystem::current_path());
    CHECK(parsed.ok());
    CHECK_EQ(parsed.value().source, std::filesystem::path(name));
    CHECK_EQ(parsed.value().args[0].classification, Class::input);
  }
}
TEST(command_modelled_row) {
  for (const auto* flag : {"-m32", "-m64", "-funsigned-char", "-fsigned-char", "-fshort-wchar", "-fno-exceptions", "-fno-rtti", "-std=gnu++23", "-march=native", "-mtune=generic"})
    classified({flag}, Class::modelled);
}
TEST(command_joined_separate_forms) {
  for (const auto* flag : {"-I", "-iquote", "-isystem", "-D", "-U", "-o", "-MF", "-MT", "-l", "-L"}) {
    const auto separate = command({flag, "value"});
    const auto joined = command({std::string(flag) + "value"});
    CHECK_EQ(separate.args[1].option, flag);
    CHECK_EQ(separate.args[1].value, "value");
    CHECK_EQ(separate.args[1].spelling.size(), 2u);
    CHECK_EQ(joined.args[1].option, separate.args[1].option);
    CHECK_EQ(joined.args[1].value, separate.args[1].value);
    CHECK_EQ(joined.args[1].classification, separate.args[1].classification);
    CHECK_EQ(joined.args[1].spelling.size(), 1u);
  }
}
TEST(command_harmless_row) {
  for (const auto* flag : {"-O0", "-O1", "-O2", "-O3", "-Os", "-Og", "-Ofast", "-g", "-g0", "-g1", "-g2", "-g3", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wformat=2", "-Wc++20-compat", "-w", "-pedantic", "-pedantic-errors", "-pthread", "-static", "-static-libstdc++", "-static-libgcc", "-s", "-lm", "-lpthread", "-lstdc++", "-fdiagnostics-color=always", "-fmax-errors=2", "-pipe", "-fsanitize=address,undefined"})
    classified({flag}, Class::harmless);
  classified({"-l", "m"}, Class::harmless);
  CHECK(command({"-Wformat=2"}).pruningAllowed());
}
TEST(command_libraries_row) {
  for (const auto& flags : std::vector<std::vector<std::string>>{{"-lssl"}, {"-l", "ssl"}, {"-Llib"}, {"-L", "lib"}}) {
    classified(flags, Class::libraries);
    const auto parsed = command(flags);
    CHECK(!parsed.pruningAllowed());
    CHECK(!parsed.pruningAllowed().reasons.empty());
    CHECK(parsed.resolutionAllowed());
  }
}
TEST(command_controlled_row) {
  for (const auto& flags : std::vector<std::vector<std::string>>{{"-oout"}, {"-o", "out"}, {"-MD"}, {"-MMD"}, {"-MFdep"}, {"-MF", "dep"}, {"-MTtarget"}, {"-MT", "target"}})
    classified(flags, Class::controlled);
}
TEST(command_rejected_row) {
  for (const auto* flag : {"-c", "-S", "-E", "-include", "-includex", "-imacros", "-imacrosx", "-Wp,-include,x", "-Wp,anything", "-Xpreprocessor", "-Wl,-rpath,x", "-Xlinker", "-Wa,x", "-Xassembler", "-save-temps", "-save-temps=obj", "-B", "-Bpath", "--sysroot", "--sysroot=/x", "-isysroot", "-isysroot/x", "-M", "-MM"}) {
    auto parsed = parseCompileCommand({"g++", "main.cpp", flag}, std::filesystem::current_path());
    CHECK(!parsed.ok());
    CHECK(parsed.error().message.find(flag) != std::string::npos);
  }
}
TEST(command_unmodelled_row_and_warning_grammar) {
  for (const auto* flag : {"-funknown", "-O4", "-W", "-WALL", "-Wbad,name", "-Wbad.name", "-W=bad", "-gline-tables-only", "-std=", "-fsanitize="}) {
    classified({flag}, Class::unmodelled);
    const auto parsed = command({flag});
    CHECK(!parsed.pruningAllowed());
    CHECK(!parsed.resolutionAllowed());
    CHECK(parsed.resolutionAllowed().reasons[0].find(flag) != std::string::npos);
    CHECK_EQ(parsed.flags(Class::unmodelled).size(), 1u);
  }
}
TEST(command_input_errors) {
  for (auto argv : std::vector<std::vector<std::string>>{{}, {""}, {"g++"}, {"g++", "-O2"}, {"g++", "a.cpp", "b.cpp"}, {"g++", "a.cpp", "file.o"}, {"g++", "main.c"}, {"g++", ""}, {"g++", "main.cpp", "-I"}})
    CHECK(!parseCompileCommand(argv, std::filesystem::current_path()).ok());
  CHECK(!parseCompileCommand({"g++", std::string("a\0.cpp", 6)}, std::filesystem::current_path()).ok());
}
TEST(command_accessors_preserve_order) {
  const auto parsed = command({"-Ione", "-iquote", "two", "-Ithree", "-isystem", "system", "-DA=1", "-U", "A", "-DB=2", "-std=c++17", "-std=gnu++23"});
  const auto includes = parsed.userIncludePaths();
  CHECK_EQ(includes.size(), 3u);
  CHECK_EQ(includes[0].path, "one");
  CHECK_EQ(includes[1].kind, IncludeKind::quote);
  CHECK_EQ(includes[2].path, "three");
  CHECK_EQ(parsed.systemIncludePaths()[0].path, "system");
  CHECK_EQ(parsed.localMacros().size(), 3u);
  CHECK_EQ(parsed.localMacros()[1].option, "-U");
  CHECK_EQ(parsed.localMacros()[1].value, "A");
  CHECK_EQ(parsed.dialect().value(), "gnu++23");
  CHECK(!command({}).dialect());
}
TEST(command_response_quotes_escapes_nesting) {
  test::TempDirectory temp;
  temp.write("outer.rsp", "-I 'one two' -DNAME=\\\"a\\ b\\\" @nested.rsp main.cpp\n");
  temp.write("nested.rsp", "-iquote \"three four\" -UOLD -isystem five\\ six");
  auto parsed = parseCompileCommand({"g++", "@outer.rsp"}, temp.path);
  CHECK(parsed.ok());
  CHECK_EQ(parsed.value().userIncludePaths()[0].path, "one two");
  CHECK_EQ(parsed.value().userIncludePaths()[1].path, "three four");
  CHECK_EQ(parsed.value().localMacros()[0].value, "NAME=\"a b\"");
  CHECK_EQ(parsed.value().systemIncludePaths()[0].path, "five six");
}
TEST(command_response_errors_and_cycles) {
  test::TempDirectory temp;
  for (const auto& text : {"-I 'unterminated", "-I trailing\\", "@cycle.rsp"}) {
    temp.write("cycle.rsp", text);
    CHECK(!parseCompileCommand({"g++", "@cycle.rsp", "main.cpp"}, temp.path).ok());
  }
  temp.write("a.rsp", "@b.rsp"); temp.write("b.rsp", "@a.rsp");
  CHECK(!parseCompileCommand({"g++", "@a.rsp", "main.cpp"}, temp.path).ok());
  const auto missing = parseCompileCommand({"g++", "@missing.rsp", "main.cpp"}, temp.path);
  CHECK(!missing.ok());
  CHECK(missing.error().message.find("missing.rsp") != std::string::npos);
  temp.write("nul.rsp", std::string("-D X\\\0Y", 8));
  CHECK(!parseCompileCommand({"g++", "@nul.rsp", "main.cpp"}, temp.path).ok());
}
TEST(command_response_depth_limit) {
  test::TempDirectory temp;
  for (int i = 0; i < 16; ++i) temp.write(std::to_string(i) + ".rsp", i == 15 ? "main.cpp" : "@" + std::to_string(i + 1) + ".rsp");
  CHECK(parseCompileCommand({"g++", "@0.rsp"}, temp.path).ok());
  temp.write("15.rsp", "@16.rsp"); temp.write("16.rsp", "main.cpp");
  CHECK(!parseCompileCommand({"g++", "@0.rsp"}, temp.path).ok());
}
TEST(command_build_argv_generated_roundtrip) {
  const auto parsed = command({"-O2", "-Iinc", "-iquote", "quote", "-isystem", "sys", "-DLOCAL", "-U", "X", "-oout", "-MD", "-MMD", "-MF", "dep.d", "-MTtarget", "-pthread"});
  auto built = parsed.buildArgv("flat.cpp", "program", false, {"ONLINE_JUDGE", "VALUE=2"});
  CHECK_EQ(built, (std::vector<std::string>{"g++", "flat.cpp", "-O2", "-isystem", "sys", "-o", "program", "-pthread", "-DONLINE_JUDGE", "-DVALUE=2"}));
  auto reparsed = parseCompileCommand(built, parsed.cwd);
  CHECK(reparsed.ok());
  CHECK_EQ(reparsed.value().source, "flat.cpp");
  CHECK(reparsed.value().userIncludePaths().empty());
  CHECK_EQ(reparsed.value().localMacros().size(), 2u);
}
TEST(command_build_argv_baseline_roundtrip) {
  const auto parsed = command({"-I", "inc", "-DA=1", "-UB", "-std=c++20", "-o", "old"});
  const auto built = parsed.buildArgv("original.cc", "baseline", true, {}, false);
  CHECK_EQ(built, (std::vector<std::string>{"g++", "original.cc", "-I", "inc", "-DA=1", "-UB", "-std=c++20", "-o", "baseline"}));
  auto reparsed = parseCompileCommand(built, parsed.cwd);
  CHECK(reparsed.ok());
  CHECK_EQ(reparsed.value().userIncludePaths().size(), 1u);
  CHECK_EQ(reparsed.value().localMacros().size(), 2u);
  CHECK_EQ(command({}).buildArgv("x.cpp", "out", true, {}), (std::vector<std::string>{"g++", "x.cpp", "-o", "out"}));
}
