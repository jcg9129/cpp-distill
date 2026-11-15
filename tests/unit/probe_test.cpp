// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.h"
#include "core/probe/probe.h"
#include "core/process/process.h"
#include <future>

using namespace distill;
TEST(probe_search_local_gcc_fixture) {
  const auto lists = parseSearchLists(test::fixture("gcc15-search.txt"));
  CHECK(lists.ok());
  CHECK(!lists.value().directories.empty());
  bool internal = false;
  for (const auto& dir : lists.value().directories) internal |= dir.gccInternal;
  CHECK(internal);
  CHECK_EQ(test::fixture("gcc15-triple.txt"), "aarch64-linux-gnu\n");
}
TEST(probe_search_homebrew_frameworks) {
  const auto result = parseSearchLists(test::fixture("homebrew-gcc-search-synthetic.txt"));
  CHECK(result.ok());
  const auto& dirs = result.value().directories;
  CHECK_EQ(dirs.size(), 6u);
  CHECK_EQ(dirs[0].kind, IncludeKind::quote);
  CHECK_EQ(dirs[0].path, "/Users/developer/quote headers");
  CHECK(!dirs[1].gccInternal);
  CHECK(dirs[2].gccInternal); CHECK(dirs[3].gccInternal);
  CHECK(dirs[5].framework);
  CHECK_EQ(dirs[5].path, "/System/Library/Frameworks");
}
TEST(probe_search_mingw_normalization) {
  const auto result = parseSearchLists(test::fixture("mingw-gcc-search-synthetic.txt"));
  CHECK(result.ok());
  const auto& dirs = result.value().directories;
  CHECK_EQ(dirs.size(), 5u);
  CHECK(dirs[1].gccInternal); CHECK(dirs[2].gccInternal);
  CHECK(!dirs[3].gccInternal);
  CHECK_EQ(pathToUtf8(dirs[4].path), "C:/Program Files/SDK/include");
}
TEST(probe_search_apple_clang) {
  const auto result = parseSearchLists(test::fixture("apple-clang-search-synthetic.txt"));
  CHECK(result.ok());
  CHECK_EQ(result.value().directories.size(), 4u);
  CHECK(result.value().directories.back().framework);
  for (const auto& dir : result.value().directories) CHECK(!dir.gccInternal);
}
TEST(probe_search_missing_or_misordered_markers) {
  for (const auto* text : {"", "random output", "#include <...> search starts here:\n/x\nEnd of search list.\n", "#include \"...\" search starts here:\n/x\n", "End of search list.\n", "#include \"...\" search starts here:\nEnd of search list.\n"})
    CHECK(!parseSearchLists(text).ok());
  CHECK(parseSearchLists("#include \"...\" search starts here:\r\n#include <...> search starts here:\r\nEnd of search list.\r\n").ok());
}
TEST(probe_internal_directory_shapes) {
  for (const auto* path : {"/usr/lib/gcc/x86_64-linux-gnu/15/include", "/usr/lib/gcc-cross/aarch64-linux-gnu/15/include-fixed", "/opt/homebrew/lib/gcc/current/gcc/aarch64-apple-darwin24/15/include", "C:\\lib\\gcc\\x86_64-w64-mingw32\\15.2.0\\include"})
    CHECK(isGccInternalIncludeDir(pathFromUtf8(path)));
  for (const auto* path : {"/usr/include/c++/15", "/usr/lib/gcc/x86_64-linux-gnu/15/../../../../x86_64-linux-gnu/include", "C:/lib/gcc/x86_64-w64-mingw32/15/../../../../x86_64-w64-mingw32/include", "/opt/homebrew/include", "/lib/gcc/no_triple/15/include", "/lib/gcc/x86_64-linux-gnu/current/include", "/lib/gcc/x86_64-linux-gnu/15/include/subdir"})
    CHECK(!isGccInternalIncludeDir(pathFromUtf8(path)));
}
TEST(probe_macros_local_gcc_fixture) {
  const auto parsed = parseMacros(test::fixture("gcc15-macros.txt"));
  CHECK(parsed.ok());
  CHECK(parsed.value().size() > 300u);
  CHECK_EQ(parsed.value().at("__GNUC__").replacement, "15");
  const auto identity = identifyCompiler(parsed.value());
  CHECK_EQ(identity.family, CompilerFamily::gcc);
  CHECK_EQ(identity.major, 15);
  CHECK_EQ(identity.minor, 2);
}
TEST(probe_macros_exact_replacements_and_functions) {
  const auto parsed = parseMacros("#define EMPTY\n#define VALUE a  + b \n#define SPACES   x  \n#define FN(x, y) ((x) + (y))\n#define V(fmt, ...) fmt, __VA_ARGS__\n#define N(args...) args\n#define Z()\n#define OBJ (x)\n");
  CHECK(parsed.ok());
  const auto& macros = parsed.value();
  CHECK_EQ(macros.at("EMPTY").replacement, "");
  CHECK_EQ(macros.at("VALUE").replacement, "a  + b ");
  CHECK_EQ(macros.at("SPACES").replacement, "  x  ");
  CHECK_EQ(macros.at("FN").parameters, (std::vector<std::string>{"x", "y"}));
  CHECK(macros.at("FN").functionLike);
  CHECK_EQ(macros.at("FN").replacement, "((x) + (y))");
  CHECK(macros.at("V").variadic);
  CHECK_EQ(macros.at("V").replacement, "fmt, __VA_ARGS__");
  CHECK(macros.at("N").variadic);
  CHECK(macros.at("Z").parameters.empty());
  CHECK(macros.at("Z").replacement.empty());
  CHECK(!macros.at("OBJ").functionLike);
  CHECK_EQ(macros.at("OBJ").replacement, "(x)");
}
TEST(probe_macros_malformed_lines) {
  for (const auto* text : {"#undef X", "#define", "#define ", "#define 9BAD 1", "#defineX 1", "#define F(x", "#define F(x,) body", "#define F(x,x) body", "#define F(..., x) body", "#define F(x)body", "#define F(x+y) body", "#define A 1\n#define A 2\n"})
    CHECK(!parseMacros(text).ok());
  CHECK(parseMacros("\n#define X 1\r\n").ok());
}
TEST(probe_identity_clang_precedes_gcc_and_apple) {
  auto macros = parseMacros(test::fixture("apple-clang-macros-synthetic.txt")).value();
  const auto apple = identifyCompiler(macros);
  CHECK_EQ(apple.family, CompilerFamily::appleClang);
  CHECK_EQ(apple.major, 17);
  CHECK_EQ(apple.minor, 0);
  macros.erase("__apple_build_version__");
  CHECK_EQ(identifyCompiler(macros).family, CompilerFamily::clang);
  CHECK_EQ(identifyCompiler({}).family, CompilerFamily::unknown);
}
TEST(probe_effective_dialects) {
  for (const auto& [number, version] : std::vector<std::pair<std::string, std::string>>{{"199711L", "98"}, {"201103L", "11"}, {"201402L", "14"}, {"201703L", "17"}, {"202002L", "20"}, {"202100L", "23"}, {"202302L", "23"}, {"202400L", "26"}}) {
    auto macros = parseMacros("#define __cplusplus " + number + '\n').value();
    auto dialect = effectiveDialect(std::nullopt, macros);
    CHECK_EQ(dialect.name, "gnu++" + version); CHECK(dialect.gnu);
    macros.emplace("__STRICT_ANSI__", Macro{false, {}, false, "1"});
    dialect = effectiveDialect(std::nullopt, macros);
    CHECK_EQ(dialect.name, "c++" + version); CHECK(!dialect.gnu);
    CHECK_EQ(dialect.cplusplus, std::stol(number));
    CHECK_EQ(effectiveDialect("gnu++2b", macros).name, "gnu++2b");
    CHECK(effectiveDialect("gnu++2b", macros).gnu);
    CHECK(!effectiveDialect("c++23", macros).gnu);
  }
}
TEST(probe_real_local_gpp) {
  const auto compiler = resolveExecutable("g++", std::filesystem::current_path());
  if (!compiler.ok()) throw test::Skip{"g++ is not on PATH"};
  test::TempDirectory temp;
  const auto cmd = parseCompileCommand({"g++", "main.cpp", "-std=c++20", "-DLOCAL=7", "-funsigned-char"}, temp.path).value();
  const auto probe = probeCompiler(compiler.value(), cmd, {true, {"DISTILL_PROBE=42"}, {}});
  if (!probe.ok()) throw std::runtime_error(probe.error().message);
  CHECK(!probe.value().triple.empty());
  CHECK(!probe.value().searchLists.directories.empty());
  CHECK(probe.value().identity.family != CompilerFamily::unknown);
  CHECK_EQ(probe.value().dialect.name, "c++20");
  CHECK_EQ(probe.value().dialect.cplusplus, 202002L);
  CHECK_EQ(probe.value().macros.at("LOCAL").replacement, "7");
  CHECK_EQ(probe.value().macros.at("DISTILL_PROBE").replacement, "42");
  CHECK(probe.value().macros.contains("__CHAR_UNSIGNED__"));
  CHECK(std::filesystem::is_empty(temp.path));
  const auto configured = probeCompiler(compiler.value(), cmd, {false, {"ONLINE_JUDGE"}, {}});
  CHECK(configured.ok());
  CHECK(!configured.value().macros.contains("LOCAL"));
  CHECK(configured.value().macros.contains("ONLINE_JUDGE"));
  CHECK(std::filesystem::is_empty(temp.path));
}
TEST(probe_failure_cleans_temporary_files) {
  test::TempDirectory temp;
  const auto cmd = parseCompileCommand({"g++", "main.cpp"}, temp.path).value();
  CHECK(!probeCompiler("distill-missing-compiler-7723", cmd).ok());
  CHECK(std::filesystem::is_empty(temp.path));
  CHECK(!probeCompiler(test::helper, cmd).ok());
  CHECK(std::filesystem::is_empty(temp.path));
}
TEST(probe_temporary_root_default_and_override) {
  test::TempDirectory temp;
  const auto cmd = parseCompileCommand({"g++", "main.cpp"}, temp.path).value();
  for (const bool overrideRoot : {false, true}) {
    ProbeOptions options;
    if (overrideRoot) options.temporaryRoot = temp.path;
    const auto result = probeCompiler(test::helper, cmd, options);
    CHECK(!result.ok());
    const std::string prefix = "compiler probe failed: ";
    CHECK(result.error().message.starts_with(prefix));
    const auto source = pathFromUtf8(result.error().message.substr(prefix.size()));
    CHECK_EQ(source.filename(), "empty.cpp");
    CHECK(std::filesystem::equivalent(source.parent_path().parent_path(),
        overrideRoot ? temp.path : std::filesystem::temp_directory_path()));
    CHECK(!std::filesystem::exists(source.parent_path()));
    CHECK(std::filesystem::is_empty(temp.path));
  }
}
