// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.h"
#include "core/inline/inline.h"
#include <algorithm>
using namespace distill;
namespace {
struct Fixture {
  test::TempDirectory temp;
  CompileCommand command;
  IncludeSearch search;
  Fixture(std::vector<std::string> flags = {}) {
    std::vector<std::string> args{"g++"}; args.insert(args.end(), flags.begin(), flags.end()); args.push_back("main.cpp");
    command = parseCompileCommand(args, temp.path).value();
    search = buildIncludeSearch(command, {}).value();
  }
  Result<InlineResult> run(InlineOptions options = {}) { return inlineSources(command, search, options); }
};
}
TEST(inline_search_precedence_spelling_and_duplicates) {
  Fixture f({"-I", "./inc", "-iquote", "q", "-I", "inc/../inc", "-isystem", "sys", "-I", "./sys"});
  for (const auto* d : {"inc", "q", "sys"}) std::filesystem::create_directory(f.temp.path / d);
  for (const auto* p : {"inc/a.h", "q/a.h", "sys/s.h"}) f.temp.write(p, "// header\n");
  const auto search = buildIncludeSearch(f.command, SearchLists{{{"sys", IncludeKind::system, false, true}}});
  CHECK(search.ok()); CHECK_EQ(search.value().quote.size(), 1u); CHECK_EQ(search.value().angle.size(), 2u);
  CHECK_EQ(pathToUtf8(search.value().angle[0].path), "./inc"); CHECK(search.value().angle[1].system);
  auto q = lookupHeader(search.value(), "a.h", false, "main.cpp").value(); CHECK_EQ(q.fileName, "q/a.h");
  auto a = lookupHeader(search.value(), "a.h", true, "main.cpp").value(); CHECK_EQ(a.fileName, "./inc/a.h");
  auto s = lookupHeader(search.value(), "s.h", true, "main.cpp").value(); CHECK(s.system);
  f.temp.write("a.h", "// next to includer\n");
  CHECK_EQ(lookupHeader(search.value(), "a.h", false, "./main.cpp").value().fileName, "./a.h");
}
TEST(inline_absolute_paths_classified_by_system_directory) {
  Fixture f({"-isystem", "sys"}); std::filesystem::create_directory(f.temp.path / "sys");
  f.temp.write("sys/a.h", "// system\n"); f.temp.write("b.h", "// user\n");
  auto a = lookupHeader(f.search, pathToUtf8(f.temp.path / "sys/a.h"), false, "main.cpp").value();
  CHECK(a.found); CHECK(a.system);
  auto b = lookupHeader(f.search, pathToUtf8(f.temp.path / "b.h"), true, "main.cpp").value();
  CHECK(b.found); CHECK(!b.system);
}
TEST(inline_repeated_headers_and_metadata) {
  Fixture f;
  f.temp.write("main.cpp", "#include \"a.h\"\n#include \"a.h\"\nint main(){}\n");
  f.temp.write("a.h", "// untouched\nconst int x=1;\n");
  const auto r = f.run(); CHECK(r.ok());
  CHECK_EQ(r.value().userFiles.size(), 2u); CHECK_EQ(r.value().userFiles[1].times, 2u);
  CHECK_EQ(r.value().userFiles[1].size, 28u);
  CHECK(r.value().text.find("// untouched\nconst int x=1;\n") != std::string::npos);
  CHECK_EQ(r.value().lineMap.size(), static_cast<std::size_t>(std::count(r.value().text.begin(), r.value().text.end(), '\n')));
  bool mapped = false;
  for (const auto& l : r.value().lineMap) if (l && l->file == "a.h" && l->line == 2) mapped = true;
  CHECK(mapped);
}
TEST(inline_system_metadata_and_missing_angle) {
  Fixture f({"-isystem", "sys"}); std::filesystem::create_directory(f.temp.path / "sys"); f.temp.write("sys/vector", "// fixture\n");
  f.temp.write("main.cpp", "/* prefix \\\n*/ #include \"vector\"\n#if 0\n#include <not_here>\n#endif\n");
  auto r = f.run(); CHECK(r.ok()); CHECK_EQ(r.value().systemIncludes.size(), 2u);
  const auto& a = r.value().systemIncludes[0];
  CHECK_EQ(a.location.line, 2u);
  CHECK(a.rewritten); CHECK(!a.angle); CHECK(a.found); CHECK(a.standard); CHECK_EQ(a.directory.value(), "sys");
  CHECK(r.value().text.find("#include <vector>") != std::string::npos);
  CHECK(!r.value().systemIncludes[1].found); CHECK(!r.value().systemIncludes[1].standard);
}
TEST(inline_rejected_inputs_have_file_and_line) {
  for (const auto* source : {"#include \"missing.h\"", "#include MACRO", "#include <x> junk", "#include_next <x>", "#import <x>", "#line 9", "#pragma GCC system_header", "__INCLUDE_LEVEL__", "__BASE_FILE__", "__builtin_COLUMN", "__builtin_source_location", "source_location", "#if 1", "#endif", "#else", "#define X 1\n#pragma once", "#if 0\n#pragma once\n#endif", "%:include <x>"}) {
    Fixture f; f.temp.write("main.cpp", source); const auto r = f.run(); CHECK(!r.ok());
    if (!r.ok()) CHECK(r.error().message.starts_with("main.cpp:"));
  }
}
TEST(inline_has_include_user_operands_rejected_system_allowed) {
  for (const auto* source : {"#if __has_include(\"a.h\")\n#endif\n", "#if __has_include_next(<a.h>)\n#endif\n"}) {
    Fixture f({"-I", "."}); f.temp.write("a.h", "// header\n"); f.temp.write("main.cpp", source);
    CHECK(!f.run().ok());
  }
  Fixture f; f.temp.write("main.cpp", "#if __has_include(<missing>)\n#endif\n"); CHECK(f.run().ok());
}
TEST(inline_flags_outside_comments_and_literals) {
  Fixture f;
  f.temp.write("main.cpp", "__COUNTER__\n#pragma push_macro(\"X\")\n#pragma pop_macro(\"X\")\n_Pragma(\"message hi\")\n");
  auto r = f.run(); CHECK(r.ok()); CHECK(r.value().flags.counter); CHECK(r.value().flags.pushMacro); CHECK(r.value().flags.popMacro); CHECK(r.value().flags.pragmaOperator);
  f.temp.write("main.cpp", "// __COUNTER__\n\"_Pragma __BASE_FILE__\"\n"); r = f.run(); CHECK(r.ok()); CHECK(!r.value().flags.counter); CHECK(!r.value().flags.pragmaOperator);
}
TEST(inline_once_collision_rehash_after_all_files_read) {
  Fixture f;
  f.temp.write("a.h", "// code before first directive is permitted\nint value;\n#pragma once\n");
  const auto id = fileIdentity(f.temp.path / "a.h").value();
  const auto guard = onceGuardName(id);
  f.temp.write("main.cpp", "#include \"a.h\"\nint " + guard + ";\n#include \"a.h\"\n");
  const auto r = f.run(); CHECK(r.ok());
  CHECK(r.value().text.find("#ifndef " + guard + "\n") == std::string::npos);
  CHECK(r.value().text.find("#ifndef " + onceGuardName(id, 1) + "\n") != std::string::npos);
  CHECK(r.value().text.find("#pragma once") == std::string::npos);
}
TEST(inline_once_identity_aliases) {
#ifndef _WIN32
  Fixture f; f.temp.write("a.h", "#pragma once\nint value;\n");
  std::filesystem::create_symlink("a.h", f.temp.path / "alias.h");
  f.temp.write("main.cpp", "#include \"a.h\"\n#include \"alias.h\"\n");
  auto r = f.run(); CHECK(r.ok()); CHECK_EQ(r.value().userFiles.size(), 3u);
  CHECK_EQ(r.value().userFiles[1].identity, r.value().userFiles[2].identity);
  auto guard = onceGuardName(r.value().userFiles[1].identity);
  auto pos = r.value().text.find("#ifndef " + guard); CHECK(pos != std::string::npos);
  CHECK(r.value().text.find("#ifndef " + guard, pos + 1) != std::string::npos);
#else
  throw test::Skip{"symlink creation requires Windows privileges"};
#endif
}
TEST(inline_recursion_guard_forms_and_pragma_once) {
  for (const auto* opening : {"#ifndef G", "#if !defined G", "#if !defined(G)", "#pragma once"}) {
    Fixture f; f.temp.write("main.cpp", "#include \"a.h\"\n");
    std::string header = std::string(opening) + "\n";
    if (std::string_view(opening) != "#pragma once") header += "#define G\n";
    header += "#include \"b.h\"\n";
    if (std::string_view(opening) != "#pragma once") header += "#endif\n";
    f.temp.write("a.h", header); f.temp.write("b.h", "#include \"a.h\"\n");
    auto r = f.run(); CHECK(r.ok()); CHECK_EQ(r.value().userFiles.size(), 3u);
  }
}
TEST(inline_recursion_rejects_each_unsafe_condition) {
  for (const auto* header : {
      "#include \"a.h\"\n",
      "int outside;\n#ifndef G\n#define G\n#include \"a.h\"\n#endif\n",
      "#ifndef G\n#include \"a.h\"\n#define G\n#endif\n",
      "#ifndef G\n#define G\n#include \"a.h\"\n#endif\n#undef G\n",
      "#ifndef _G\n#define _G\n#include \"a.h\"\n#endif\n",
      "#ifndef INT_MAX\n#define INT_MAX\n#include \"a.h\"\n#endif\n",
      "#ifndef G\n#define G\n#pragma push_macro(\"X\")\n#include \"a.h\"\n#endif\n",
      "#ifndef G\n#define G\n#include \"a.h\"\n#pragma pop_macro(\"X\")\n#endif\n",
      "#ifndef G\n#define G\n#include \"a.h\"\n_Pragma(\"x\")\n#endif\n",
      "#ifndef G\n#define G\n#include <vendor.h>\n#include \"a.h\"\n#endif\n",
      "#ifndef G\n#define G\n#include \"a.h\"\n#else\nint other;\n#endif\n"}) {
    Fixture f; f.temp.write("main.cpp", "#include \"a.h\"\n"); f.temp.write("a.h", header);
    const auto r = f.run(); CHECK(!r.ok()); if (!r.ok()) CHECK(r.error().message.find("cycle:") != std::string::npos);
  }
}
TEST(inline_recursion_checks_later_global_undef_and_ancestor_paths) {
  Fixture f; f.temp.write("main.cpp", "#include \"a.h\"\n#undef G\n");
  f.temp.write("a.h", "#ifndef G\n#define G\n#include \"b.h\"\n#endif\n");
  f.temp.write("b.h", "#include \"a.h\"\n"); CHECK(!f.run().ok());
  f.temp.write("main.cpp", "#include \"a.h\"\n");
  f.temp.write("b.h", "#include <vendor.h>\n#include \"a.h\"\n"); CHECK(!f.run().ok());
  f.temp.write("b.h", "#include <vector>\n#include \"a.h\"\n"); CHECK(f.run().ok());
  InlineOptions options; options.names.extensions["fixture"].insert("G"); CHECK(!f.run(options).ok());
  options.library = "other"; CHECK(f.run(options).ok());
  options.library = "fixture"; CHECK(!f.run(options).ok());
}
TEST(inline_limits_depth_and_output_include_markers) {
  Fixture f; f.temp.write("main.cpp", "#include \"a.h\"\n"); f.temp.write("a.h", "#include \"b.h\"\n"); f.temp.write("b.h", "int value;\n");
  InlineOptions options; options.maxIncludeDepth = 2; CHECK(!f.run(options).ok());
  options.maxIncludeDepth = 3; CHECK(f.run(options).ok());
  options.maxOutputBytes = 80; auto r = f.run(options); CHECK(!r.ok());
  if (!r.ok()) CHECK(r.error().message.find("size limit") != std::string::npos);
  // A long include directive can shrink below the output cap.
  f.temp.write("main.cpp", "#" + std::string(2048, ' ') + "include \"a.h\"\n");
  f.temp.write("a.h", "int value;\n"); options.maxOutputBytes = 100;
  auto small = f.run(options); CHECK(small.ok());
  if (small.ok()) {
    options.maxOutputBytes = small.value().text.size(); CHECK(f.run(options).ok());
    --options.maxOutputBytes; CHECK(!f.run(options).ok());
  }
}
TEST(inline_marker_filename_escaping_and_unmodified_crlf) {
#ifndef _WIN32
  Fixture f; f.temp.write("main.cpp", "#include \"a\\b.h\"\n"); f.temp.write("a\\b.h", "int x;\r\n#include \"next.h\"\n");
  f.temp.write("next.h", "int y;\n");
  auto r = f.run(); CHECK(r.ok()); CHECK(r.value().text.find("#line 1 \"a\\\\b.h\"") != std::string::npos);
  CHECK(r.value().text.find("int x;\r\n") != std::string::npos);
#endif
}

TEST(inline_marker_escapes_quote_in_search_directory) {
#ifndef _WIN32
  Fixture f({"-I", "quoted\"dir"});
  std::filesystem::create_directory(f.temp.path / "quoted\"dir");
  f.temp.write("main.cpp", "#include <a.h>\n"); f.temp.write("quoted\"dir/a.h", "int x;\n");
  auto r = f.run(); CHECK(r.ok());
  CHECK(r.value().text.find("#line 1 \"quoted\\\"dir/a.h\"") != std::string::npos);
#else
  throw test::Skip{"Windows filenames cannot contain quotes"};
#endif
}
