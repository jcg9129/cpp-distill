// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.h"
#include "core/lex/scan.h"
using namespace distill;
namespace {
std::vector<std::string> directives(const ScanResult& scan) {
  std::vector<std::string> result;
  for (const auto& line : scan.lines) if (line.directive) result.push_back(line.directive->name);
  return result;
}
std::vector<std::string> ids(const ScanResult& scan) {
  std::vector<std::string> result;
  for (const auto& t : scan.identifiers) result.push_back(t.text);
  return result;
}
}
TEST(lex_comments_before_hash_and_across_lines) {
  auto s = scanDirectives("/* x */ # include \"a.h\" // end\n/*\n#include <bad>\n*/ #define OK 1\n// #error\n");
  CHECK(s.ok());
  CHECK_EQ(directives(s.value()), (std::vector<std::string>{"include", "define"}));
  CHECK_EQ(s.value().lines[0].directive->tokens[0].text, "\"a.h\"");
  CHECK_EQ(s.value().lines[1].directive->tokens[0].line, 4u);
}
TEST(lex_splices_everywhere_and_physical_ranges) {
  auto s = scanDirectives("// comment\\\n#include <no>\n/* com\\\nment */ #inc\\\nlude <a.h>\n\"a\\\nb\" ID\n");
  CHECK(s.ok());
  CHECK_EQ(directives(s.value()), (std::vector<std::string>{"include"}));
  CHECK_EQ(s.value().lines[0].firstLine, 1u); CHECK_EQ(s.value().lines[0].lastLine, 2u);
  CHECK_EQ(s.value().lines[1].firstLine, 3u); CHECK_EQ(s.value().lines[1].lastLine, 5u);
  CHECK_EQ(s.value().identifiers.back().text, "ID"); CHECK_EQ(s.value().identifiers.back().line, 7u);
  CHECK_EQ(s.value().lines[1].directive->tokens[0].text, "<a.h>");
}
TEST(lex_raw_strings_all_prefixes_and_fake_directives) {
  for (const auto* prefix : {"R", "u8R", "LR", "uR", "UR"}) {
    const auto source = std::string(prefix) + "\"tag(\n#include \"bad.h\"\n)tag\" # nope\n#include <ok>\n";
    const auto s = scanDirectives(source);
    CHECK(s.ok()); CHECK_EQ(directives(s.value()), (std::vector<std::string>{"include"}));
    CHECK_EQ(s.value().identifiers.size(), 2u); // nope, include; no literal contents.
  }
}
TEST(lex_escaped_literals_and_digit_separators) {
  auto s = scanDirectives("char c='\\\''; const char* s=\"\\\" #include fake\"; auto n=1'000'123; auto h=0xA'B; real;\n");
  CHECK(s.ok()); CHECK(directives(s.value()).empty());
  CHECK_EQ(ids(s.value()), (std::vector<std::string>{"char", "c", "const", "char", "s", "auto", "n", "auto", "h", "real"}));
}
TEST(lex_unbalanced_literals_are_lenient) {
  const auto s = scanDirectives("#if 0\n' unbalanced\n\" unbalanced\n#endif\n#include \"ok\"\n");
  CHECK(s.ok()); CHECK_EQ(directives(s.value()), (std::vector<std::string>{"if", "endif", "include"}));
}
TEST(lex_crlf_bom_no_final_newline) {
  const std::string source = "\xef\xbb\xbf  # include \\\r\n<a.h>\r\nfinal";
  const auto s = scanDirectives(source);
  CHECK(s.ok()); CHECK_EQ(s.value().lines.size(), 2u);
  CHECK_EQ(s.value().lines[0].lastLine, 2u);
  CHECK_EQ(s.value().lines[0].text, "\xef\xbb\xbf  # include <a.h>\r\n");
  CHECK_EQ(s.value().lines[1].end, source.size()); CHECK_EQ(s.value().identifiers.back().line, 3u);
  CHECK(scanDirectives("").value().lines.empty());
  CHECK_EQ(scanDirectives("\\\n").value().lines.size(), 1u);
}
TEST(lex_header_names_do_not_interpret_comments_or_escapes) {
  const auto s = scanDirectives("#include <dir//a.h>\n#include \"back\\slash.h\"\n#if __has_include(<dir/*a.h>)\n#endif\n");
  CHECK(s.ok()); CHECK_EQ(s.value().lines[0].directive->tokens[0].text, "<dir//a.h>");
  CHECK_EQ(s.value().lines[1].directive->tokens[0].text, "\"back\\slash.h\"");
  CHECK_EQ(s.value().lines[2].directive->tokens[2].kind, TokenKind::header);
}
TEST(lex_digraph_directive_rejected_only_at_line_start) {
  CHECK(!scanDirectives(" /*x*/ %: include <x>\n").ok());
  CHECK(scanDirectives("const char* s=\"%:\";\nx %: y;\n").ok());
  CHECK(!scanDirectives("/*\n*/ %:define X\n").ok());
}
TEST(lex_identifiers_outside_literals_and_comments_only) {
  const auto s = scanDirectives("FOO /* HIDDEN */ \"STRING\" 'c' // COMMENT\nBAR __COUNTER__\n");
  CHECK(s.ok()); CHECK_EQ(ids(s.value()), (std::vector<std::string>{"FOO", "BAR", "__COUNTER__"}));
  CHECK_EQ(s.value().identifiers[1].line, 2u);
}
TEST(lex_maximal_preprocessing_tokens) {
  const auto s = scanDirectives("a++ + b >>= 1; x <=> y; z<::T>;\n");
  CHECK(s.ok()); CHECK_EQ(s.value().lines[0].tokens[1].text, "++");
  CHECK_EQ(s.value().lines[0].tokens[4].text, ">>=");
}
TEST(lex_raw_close_cannot_be_synthesized_by_splicing) {
  const auto s = scanDirectives("R\"x(body )x\\\n\"\n#include \"not-a-header\"\n)x\"\n#include <real>\n");
  CHECK(s.ok()); CHECK_EQ(directives(s.value()), (std::vector<std::string>{"include"}));
  CHECK_EQ(s.value().lines.back().directive->tokens[0].text, "<real>");
}
TEST(lex_multiline_raw_macro_keeps_trailing_tokens_and_original_contents) {
  const auto s = scanDirectives("#define TEXT R\"(one\\\ntwo\nthree)\" + SUFFIX\n#include <real>\n");
  CHECK(s.ok()); CHECK_EQ(s.value().lines.size(), 2u);
  const auto& l = s.value().lines[0];
  CHECK_EQ(l.firstLine, 1u); CHECK_EQ(l.lastLine, 3u);
  CHECK_EQ(l.directive->tokens.size(), 4u);
  CHECK_EQ(l.directive->tokens[1].text, "R\"(one\\\ntwo\nthree)\"");
  CHECK_EQ(l.directive->tokens[3].text, "SUFFIX");
  CHECK_EQ(l.directive->tokens[3].line, 3u);
}
TEST(lex_whitespace_splices_and_raw_restoration) {
  for (const auto* gap : {" ", "\t", "\f", "\v", " \t\f\v"}) {
    for (const auto* newline : {"\n", "\r\n"}) {
      const auto splice = std::string("\\") + gap + newline;
      const auto source = "// note" + splice + "#include <hidden>\n/* co" + splice +
          "mment */ #inc" + splice + "lude <real>\n\"a" + splice + "b\"\n";
      const auto s = scanDirectives(source);
      CHECK(s.ok()); CHECK_EQ(directives(s.value()), (std::vector<std::string>{"include"}));
      CHECK_EQ(s.value().lines[1].directive->tokens[0].text, "<real>");
      CHECK_EQ(s.value().lines.back().tokens[0].text, "\"ab\"");
      CHECK_EQ(s.value().lines.back().tokens[0].line, 6u);
      const auto raw = scanDirectives("R\"(a" + splice + "b)\"\n");
      CHECK(raw.ok());
      CHECK_EQ(raw.value().lines[0].tokens[0].text, std::string("R\"(a\\ \nb)\""));
    }
  }
  CHECK_EQ(scanDirectives("\\ \t").value().lines[0].tokens[0].text, "\\");
  const auto raw = scanDirectives("R\"x(body )x\\ \t\n\"\n#include <hidden>\n)x\"\n#include <real>\n");
  CHECK(raw.ok()); CHECK_EQ(directives(raw.value()), (std::vector<std::string>{"include"}));
  CHECK_EQ(raw.value().lines.back().directive->tokens[0].text, "<real>");
}
TEST(lex_block_comment_logical_lines_and_directive_extent) {
  const std::string source = "int x; /* c\n */ #include <not-a-directive>\n"
      "#include /* operand\n */ \"a.h\" /* tail\n */ int ignored;\n"
      "#define X /* replacement\n */ 1\nnext\n";
  const auto s = scanDirectives(source);
  CHECK(s.ok()); CHECK_EQ(s.value().lines.size(), 4u);
  CHECK(!s.value().lines[0].directive);
  const auto& include = s.value().lines[1];
  CHECK_EQ(include.firstLine, 3u); CHECK_EQ(include.lastLine, 5u);
  CHECK_EQ(include.directive->tokens.size(), 4u);
  CHECK_EQ(include.directive->tokens[0].kind, TokenKind::header);
  CHECK_EQ(include.directive->tokens[0].line, 4u);
  CHECK_EQ(include.directive->tokens[1].text, "int");
  CHECK_EQ(include.directive->end, source.find("#define"));
  CHECK_EQ(s.value().lines[2].directive->tokens.size(), 2u);
  CHECK_EQ(s.value().lines[2].directive->tokens[1].text, "1");
  CHECK_EQ(s.value().lines[2].directive->tokens[1].line, 7u);
  CHECK_EQ(s.value().lines[3].tokens[0].line, 8u);
  CHECK(!scanDirectives("int x; /*\n*/ %:include <x>\n").value().lines[0].directive);
}
