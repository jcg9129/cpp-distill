// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.h"
#include "core/render/render.h"
#include <algorithm>

using namespace distill;
namespace {
template<class T> T take(Result<T> r) { if (!r.ok()) throw std::runtime_error(r.error().message); return std::move(r.value()); }
PruningModel source(std::string text, std::size_t first = 1, std::string file = "a.cpp") {
  PruningModel m; m.text = std::move(text); m.dialect = "c++20";
  for (std::size_t i = 0; i < m.text.size(); ++i) if (!i || m.text[i-1] == '\n') m.lineMap.push_back(SourceLocation{file,first++});
  return m;
}
RenderedSource render(PruningModel m, std::vector<EditRange> deletions, bool strip = false) {
  PruningResult r;
  for (auto d : deletions) { PruneFamily f; f.ranges = {d}; m.families.push_back(f); r.families.push_back({}); }
  auto out = take(renderSource(m,&r,strip));
  take(renderingInvariant(m,&r,out.provenance.text));
  take(staticNameInvariant(m,r,out.provenance.text));
  return out;
}
std::string stripped(const PruningModel& m) {
  auto out = take(renderSource(m,nullptr,true));
  take(renderingInvariant(m,nullptr,out.provenance.text));
  return out.provenance.text;
}
}
TEST(render_whole_lines_and_shared_line_exact) {
  CHECK_EQ(render(source("int dead;\nint live;\n"),{{0,9}}).provenance.text,"#line 2 \"a.cpp\"\nint live;\n");
  CHECK_EQ(render(source("int a; int dead; int b;\n"),{{7,16}}).provenance.text,"#line 1 \"a.cpp\"\nint a;   int b;\n");
  CHECK_EQ(render(source("int dead;\n"),{{0,9}}).provenance.text,"");
  CHECK_EQ(render(source("int dead;"),{{0,9}}).provenance.text,"");
  CHECK_EQ(render(source("int a; int dead;"),{{7,16}}).provenance.text,"#line 1 \"a.cpp\"\nint a;  ");
}
TEST(render_comments_only_inside_deletions) {
  CHECK_EQ(render(source("/*keep*/ int dead; //stay\nint b;\n"),{{9,18}}).provenance.text,
           "#line 1 \"a.cpp\"\n/*keep*/   //stay\nint b;\n");
  CHECK_EQ(render(source("int /*gone*/ dead;\n//stay\nint b;\n"),{{0,18}}).provenance.text,
           "#line 2 \"a.cpp\"\n//stay\nint b;\n");
}
TEST(render_separator_exhaustive_pairs) {
  // Every pair is separated by an erased token, including pp-number exponents,
  // comment introducers, digraphs, literal prefixes and multi-character operators.
  const std::vector<std::string> words = {"a","b","1","1e","0x1p",".","+","-","/","*",">","<","=","&","|",":","%","#","u8","R","\"s\"","'c'","_suffix"};
  std::size_t checked = 0;
  for (const auto& a : words) for (const auto& b : words) {
    if (a == "#") continue; // A directive is intentionally undeletable.
    const auto text = a + " erased " + b + '\n';
    const auto out = render(source(text),{{a.size()+1,a.size()+7}});
    CHECK_EQ(out.provenance.text,"#line 1 \"a.cpp\"\n" + a + "   " + b + '\n'); ++checked;
  }
  CHECK_EQ(checked,506u);
}
TEST(render_no_insertion_inside_multiline_tokens) {
  const std::string text = "int dead;\nconst char* p=R\"tag(\n#line 99 \"fake\"\n// not a comment\n)tag\"; /*keep\n#not_a_directive\n*/ int x;\n";
  CHECK_EQ(render(source(text),{{0,9}}).provenance.text,"#line 2 \"a.cpp\"\n" + text.substr(10));
}
TEST(render_generated_marker_inside_removed_function) {
  auto m = source("int gone(){\n#line 40 \"a.cpp\"\nreturn 1;\n}\nint main(){return __LINE__;}\n");
  m.lineMap[1].reset(); m.lineMap[2] = SourceLocation{"a.cpp",40};
  m.lineMap[3] = SourceLocation{"a.cpp",41}; m.lineMap[4] = SourceLocation{"a.cpp",42};
  const auto end = m.text.find("int main");
  CHECK_EQ(render(m,{{0,end}}).provenance.text,"#line 42 \"a.cpp\"\nint main(){return __LINE__;}\n");
}
TEST(render_generated_guards_require_original_presumed_file) {
  auto m = source("#line 1 \"a.h\"\n#ifndef G\n#define G\n#line 1 \"a.h\"\nint live;\n#endif\n");
  for (auto& at : m.lineMap) at.reset();
  m.lineMap[4] = SourceLocation{"a.h",1};
  PruningResult r;
  CHECK_EQ(take(renderSource(m,&r)).provenance.text,
           "#line 1 \"a.h\"\n#ifndef G\n#line 2 \"a.h\"\n#define G\n#line 1\nint live;\n#endif\n");
  // Missing initial provenance must fail instead of introducing flat.cpp.
  m.text.erase(0,m.text.find('\n')+1); m.lineMap.erase(m.lineMap.begin());
  auto invalid = renderSource(m,&r);
  CHECK(!invalid.ok());
  CHECK(invalid.error().message.find("missing source location") != std::string::npos);
}
TEST(render_multiple_files_crlf_and_no_final_newline) {
  auto m = source("int dead;\r\nint b;\r\nint c;",7,"one\\a\".h");
  m.lineMap[2] = SourceLocation{"other.cpp",42};
  CHECK_EQ(render(m,{{0,9}}).provenance.text,
           "#line 8 \"one\\\\a\\\".h\"\nint b;\r\n#line 42 \"other.cpp\"\nint c;");
}
TEST(render_rejects_partial_token_comment_and_directive) {
  for (const auto& pair : std::vector<std::pair<std::string,EditRange>>{{"int dead;\n",{1,8}}, {"a /*keep*/ b\n",{3,6}}, {"#define A 1\n",{0,12}}}) {
    auto m = source(pair.first); PruneFamily f; f.ranges = {pair.second}; m.families = {f};
    PruningResult r; r.families.resize(1);
    auto out = renderSource(m,&r); CHECK(!out.ok());
    if (!out.ok()) CHECK(out.error().message.find(" at ") != std::string::npos);
  }
}
TEST(render_strip_comments_byte_exact) {
  CHECK_EQ(stripped(source("a/**/b; // tail\n/*two\nthree\n*/ c;\n")),
           "#line 1 \"a.cpp\"\na b;  \n\n\n c;\n");
  CHECK_EQ(stripped(source("a/*one\r\ntwo*/b;\r\n")),"#line 1 \"a.cpp\"\na\r\nb;\r\n");
  CHECK_EQ(stripped(source("a /\\\n*comment*\\\n/ b;\n")),"#line 1 \"a.cpp\"\na \n\n b;\n");
  CHECK_EQ(stripped(source("a //one\\\ntwo\nb;")),"#line 1 \"a.cpp\"\na \n\nb;");
}
TEST(render_strip_preserves_literals_headers_and_raw_strings) {
  const std::string text = "#include <a//b>\nconst char*s=\"/*stay*/\";\nauto r=R\"x(//stay\n/*stay*/)x\"; //gone\n";
  CHECK_EQ(stripped(source(text)),"#line 1 \"a.cpp\"\n" + text.substr(0,text.find("//gone")) + " \n");
}
TEST(render_short_markers_not_after_kept_conditionals) {
  // The X branch ends in b.h and the other in a.h: the marker after #endif must
  // name a.h although the textually previous marker names a.h too.
  auto m = source("#line 1 \"a.h\"\nint a;\n#if X\n#line 1 \"b.h\"\nint b;\n#else\nint c;\n#endif\nint d;\nint gone;\nint e;\n");
  m.lineMap = {std::nullopt,SourceLocation{"a.h",1},SourceLocation{"a.h",2},std::nullopt,SourceLocation{"b.h",1},
               SourceLocation{"a.h",4},SourceLocation{"a.h",5},SourceLocation{"a.h",6},SourceLocation{"a.h",7},
               SourceLocation{"a.h",8},SourceLocation{"a.h",9}};
  const auto gone = m.text.find("int gone;\n");
  const auto text = render(m,{{gone,gone+10}}).provenance.text;
  CHECK_EQ(text,"#line 1 \"a.h\"\nint a;\n#if X\n#line 1 \"b.h\"\nint b;\n#line 4 \"a.h\"\n#else\n#line 5 \"a.h\"\nint c;\n"
                "#endif\n#line 7 \"a.h\"\nint d;\n#line 9\nint e;\n");
  // The independent re-lex rejects a short marker whose segment has no marker.
  PruningResult r; PruneFamily f; f.ranges = {{gone,gone+10}}; m.families.push_back(f); r.families.push_back({});
  for (const auto& [from, to] : {std::pair{"#line 7 \"a.h\"", "#line 7"}, {"#line 5 \"a.h\"", "#line 5"}}) {
    auto forged = text; forged.replace(forged.find(from), std::string_view(from).size(), to);
    CHECK(!renderingInvariant(m,&r,forged).ok());
  }
  CHECK(renderingInvariant(m,&r,text).ok());
}
TEST(render_strip_multiline_define_resynchronizes) {
  CHECK_EQ(stripped(source("#define VALUE 1 /* one\ntwo\nthree */ + 2\nint x=VALUE+__LINE__;\n")),
           "#line 1 \"a.cpp\"\n#define VALUE 1   + 2\n#line 4\nint x=VALUE+__LINE__;\n");
  CHECK_EQ(stripped(source("#define VALUE(x) \\\n  (x /*one\ntwo*/ + 2)\nint y=__LINE__;\n")),
           "#line 1 \"a.cpp\"\n#define VALUE(x) \\\n  (x   + 2)\n#line 4\nint y=__LINE__;\n");
}
TEST(render_strip_unsafe_comment_stays) {
  const std::string text = "int x; /*one\ntwo*/ #pragma pack(1)\n";
  CHECK_EQ(stripped(source(text)),"#line 1 \"a.cpp\"\n" + text);
}
TEST(render_strip_conditional_directive_comment_stays_when_resync_can_be_skipped) {
  const std::string text = "#if 0\n#define X /*a\nb*/ 1\n#elif __LINE__ == 4\nint x=__LINE__;\n#endif\nint y=__LINE__;\n";
  CHECK_EQ(stripped(source(text)),"#line 1 \"a.cpp\"\n" + text);
}
TEST(render_maps_all_offset_provenance) {
  auto m = source("// distill: keep\nint keep;\nint gone;\nint last;\n");
  m.keepOffsets = take(captureKeepOffsets(m.text)); m.annotations = {{m.keepOffsets.front(),0}};
  PruneMacroExpansion expansion; expansion.offset = m.text.find("last"); expansion.name = "last";
  m.expansions.push_back(expansion);
  PruneFamily kept, gone; kept.ranges = {{m.text.find("int keep"),m.text.find("int gone")}};
  gone.ranges = {{m.text.find("int gone"),m.text.find("int last")}}; m.families = {kept,gone};
  PruningResult r; r.families.resize(2); r.families[0].kept = true;
  auto out = take(renderSource(m,&r,true));
  take(renderingInvariant(m,&r,out.provenance.text));
  const auto& p = out.provenance;
  CHECK_EQ(p.keepOffsets.front(),p.text.find("int keep"));
  CHECK_EQ(p.annotations.front().offset,p.keepOffsets.front());
  CHECK_EQ(p.expansions.front().offset,p.text.find("last"));
  CHECK_EQ(p.families[0].ranges.front().begin,p.text.find("int keep"));
  CHECK(p.families[1].ranges.empty()); CHECK(!out.mapOffset(m.text.find("int gone")));
  CHECK_EQ(p.lineMap.back()->line,4u);
}
TEST(render_invariant_first_mismatch_and_boundaries) {
  auto m = source("#pragma one\nint x; int y;\n");
  const auto good = take(renderSource(m)).provenance.text;
  for (const auto& corrupt : {std::string("#line 2 \"a.cpp\"\n#pragma one\nint x; int y;\n"),
                             std::string("#line 1 \"wrong\"\n#pragma one\nint x; int y;\n"),
                             std::string("#line 1 \"a.cpp\"\n#pragma one int x; int y;\n"),
                             good + "more"}) {
    auto check = renderingInvariant(m,nullptr,corrupt); CHECK(!check.ok());
    if (!check.ok()) CHECK(check.error().message.find("rendering invariant at token ") != std::string::npos);
  }
  CHECK(renderingInvariant(m,nullptr,good).ok());
}
TEST(render_static_invariant_on_final_text) {
  auto m = source("int dead;\nint main(){}\n");
  PruneFamily f; f.names = {"dead"}; f.ranges = {{0,9}}; m.families = {f};
  PruningResult r; r.families.resize(1);
  CHECK(staticNameInvariant(m,r,take(renderSource(m,&r)).provenance.text).ok());
  auto bad = staticNameInvariant(m,r,"int main(){dead;}\n"); CHECK(!bad.ok());
  CHECK(bad.error().message.find("dead") != std::string::npos);
}
TEST(render_exhaustive_shared_line_deletion_masks) {
  const auto m = source("int a; int b;\nint c; int d;\n");
  for (unsigned mask = 0; mask < 16; ++mask) {
    std::vector<EditRange> erased;
    for (unsigned i = 0; i < 4; ++i) if (mask & (1u<<i)) erased.push_back({7*i,7*i+6});
    std::string expected;
    for (unsigned line = 0; line < 2; ++line) {
      const auto bits = (mask >> (line*2)) & 3;
      if (bits == 3) continue;
      if (expected.empty()) expected = "#line " + std::to_string(line+1) + " \"a.cpp\"\n";
      expected += bits & 1 ? " " : "int " + std::string(1,char('a'+line*2)) + ';';
      expected += ' ';
      expected += bits & 2 ? " " : "int " + std::string(1,char('b'+line*2)) + ';';
      expected += '\n';
    }
    CHECK_EQ(render(m,erased).provenance.text,expected);
  }
}
TEST(render_repeated_presumed_locations_stay_distinct) {
  auto m = source("int first;\nint second;\nint third;\n");
  m.lineMap[1] = m.lineMap[0]; m.lineMap[2] = m.lineMap[0];
  CHECK_EQ(render(m,{{0,10}}).provenance.text,
           "#line 1 \"a.cpp\"\nint second;\n#line 1\nint third;\n");
}
TEST(render_removes_empty_namespace_shell_subtrees) {
  for (const auto& text : {
      "namespace outer { namespace inner { int dead; } }\n",
      "namespace outer { inline namespace version { int dead; } }\n",
      "namespace outer::inner { int dead; }\n",
      "namespace outer::inline inner { int dead; }\n",
      "namespace { namespace { int dead; } }\n"}) {
    auto m = source(text);
    const auto begin = m.text.find("int dead;");
    CHECK_EQ(render(m,{{begin,begin+9}}).provenance.text,"");
    // The flat rendering still owns every original token.
    CHECK(take(renderSource(m)).provenance.text.find("namespace") != std::string::npos);
  }
  CHECK_EQ(render(source("namespace n {}\nnamespace n {}\n"),{}).provenance.text,"");
}
TEST(render_preserves_named_namespace_uses_and_live_contents) {
  for (const auto& use : {"using namespace n;", "namespace alias = n;", "int x = n::x;"}) {
    const auto text = std::string("namespace n { int dead; }\n") + use + '\n';
    const auto begin = text.find("int dead;");
    const auto out = render(source(text),{{begin,begin+9}}).provenance.text;
    CHECK(out.find("namespace n {") != std::string::npos);
    CHECK(out.find(use) != std::string::npos);
    CHECK(out.find("dead") == std::string::npos);
  }
  const auto text = "namespace outer { namespace inner {} }\nusing namespace outer::inner;\n";
  CHECK(render(source(text),{}).provenance.text.find("namespace inner") != std::string::npos);
  CHECK(render(source("namespace { int live; }\n"),{}).provenance.text.find("namespace {") != std::string::npos);
  CHECK(render(source("inline namespace n {}\nusing namespace n;\n"),{}).provenance.text.find("inline namespace n") != std::string::npos);
  const std::string reopened = "namespace n { int x; }\nnamespace n { int dead; }\nint f(){return n::x;}\n";
  const auto begin = reopened.find("int dead;");
  const auto out = render(source(reopened),{{begin,begin+9}}).provenance.text;
  const auto first = out.find("namespace n {");
  CHECK(first != out.npos && out.find("namespace n {",first+1) == out.npos);
}
TEST(render_namespace_names_ignore_deleted_uses_comments_and_literals) {
  const std::string text = "namespace n { int dead; }\nvoid gone(){using namespace n;}\nconst char* s=R\"(n namespace n {})\"; // n\n";
  const auto dead = text.find("int dead;"), gone = text.find("void gone"), end = text.find("const char");
  const auto out = render(source(text),{{dead,dead+9},{gone,end}}).provenance.text;
  const auto identifiers = take(pruningIdentifiers(out));
  CHECK(std::none_of(identifiers.begin(),identifiers.end(),[](const auto& t) { return t.text == "n"; }));
  CHECK(out.find("const char* s=R\"(n namespace n {})\";") != std::string::npos);
  CHECK(out.find("// n") != std::string::npos);
}
TEST(render_namespace_directives_and_provenance) {
  const std::string directive = "namespace n {\n#define X 1\n}\n";
  CHECK(render(source(directive),{}).provenance.text.find(directive) != std::string::npos);
  auto m = source("namespace n {\n#line 40 \"a.cpp\"\nint dead;\n}\nint live=__LINE__;\n");
  m.lineMap[1].reset(); m.lineMap[2] = SourceLocation{"a.cpp",40};
  m.lineMap[3] = SourceLocation{"a.cpp",41}; m.lineMap[4] = SourceLocation{"a.cpp",42};
  const auto begin = m.text.find("int dead;");
  const auto out = render(m,{{begin,begin+9}});
  CHECK_EQ(out.provenance.text,"#line 42 \"a.cpp\"\nint live=__LINE__;\n");
  CHECK_EQ(out.provenance.lineMap.back()->line,42u);
}

TEST(render_namespace_caide_unrelated_inner_names) {
  const std::string text =
      "namespace ns1 { void unused() {} }\n"
      "namespace ns1 { void used() {} }\n"
      "namespace ns2 { void used() {} }\n"
      "namespace outer1 { namespace inner1 { template<class T> struct UsedClass {}; } }\n"
      "namespace outer2 { namespace inner1 { void used() {} } }\n"
      "namespace outer3 { namespace inner1 { void unused() {} } }\n"
      "int main() { ns1::used(); ns2::used(); outer2::inner1::used(); "
      "{ outer1::inner1::UsedClass<int> x; } return 0; }\n";
  std::vector<EditRange> deletions;
  for (auto at = text.find("void unused() {}"); at != text.npos; at = text.find("void unused() {}",at+1))
    deletions.push_back({at,at+16});
  const auto out = render(source(text),deletions).provenance.text;
  CHECK(out.find("outer3") == out.npos);
  CHECK(out.find("namespace outer1") != out.npos);
  CHECK(out.find("namespace outer2") != out.npos);
  const auto first = out.find("namespace ns1");
  CHECK(first != out.npos && out.find("namespace ns1",first+1) == out.npos);
}
TEST(render_namespace_two_and_three_level_name_collisions) {
  for (const auto& body : {
      "namespace inner { int dead; }",
      "namespace middle { namespace inner { int dead; } }"}) {
    const auto text = std::string("namespace other { namespace middle { namespace inner {} } }\n") +
        "namespace outer { " + body + " }\nusing namespace other::middle::inner;\n";
    const auto dead = text.find("int dead;");
    const auto out = render(source(text),{{dead,dead+9}}).provenance.text;
    CHECK(out.find("namespace outer") == out.npos);
    CHECK(out.find("namespace other") != out.npos);
  }
  // The outer has retained contents, but a different qualification cannot
  // reach its inner namespace. Apply this at two and three levels.
  for (const auto& suffix : {std::string("inner"),std::string("middle::inner")}) {
    const auto body = suffix == "inner" ? "namespace inner {}" : "namespace middle { namespace inner {} }";
    const auto text = std::string("namespace outer { ") + body + " int live; }\n" +
        "namespace other { " + body + " }\n" +
        "using namespace other::" + suffix + ";\n";
    const auto out = render(source(text),{}).provenance.text;
    CHECK(out.find("namespace outer") != out.npos);
    CHECK(out.find(std::string("namespace outer { ") + body) == out.npos);
  }
}
TEST(render_namespace_nested_lookup_and_exposure) {
  for (const auto& use : {"using namespace outer3;", "namespace alias = outer3;"}) {
    const auto text = std::string("namespace other { namespace inner1 { int live; } }\n") +
        "namespace outer3 { namespace inner1 { int dead; } }\n" + use +
        "\nint x = other::inner1::live;\n";
    const auto dead = text.find("int dead;");
    const auto out = render(source(text),{{dead,dead+9}}).provenance.text;
    CHECK(out.find("namespace outer3") != out.npos);
    CHECK(out.find("namespace outer3 { namespace inner1") == out.npos);
    CHECK(out.find("namespace other { namespace inner1") != out.npos);
  }
  for (const auto& use : {
      "using namespace outer::middle::inner;",
      "namespace alias = outer::middle::inner;",
      "using namespace outer; using namespace middle::inner;",
      "namespace alias = outer; using namespace alias::middle::inner;"}) {
    const auto text = std::string("namespace outer { namespace middle { namespace inner {} } }\n") + use + '\n';
    const auto out = render(source(text),{}).provenance.text;
    CHECK(out.find("namespace outer") != out.npos);
    CHECK(out.find("namespace middle") != out.npos);
    CHECK(out.find("namespace inner") != out.npos);
  }
  const auto text = "namespace outer { namespace inner {} namespace middle { namespace deep {} } "
      "using namespace inner; using namespace middle::deep; }\n";
  const auto out = render(source(text),{}).provenance.text;
  CHECK(out.find("namespace inner") != out.npos);
  CHECK(out.find("namespace deep") != out.npos);
}
TEST(render_namespace_reopened_blocks_provide_required_names) {
  const auto count = [](const std::string& text,const std::string& needle) {
    std::size_t result = 0;
    for (auto at = text.find(needle); at != text.npos; at = text.find(needle,at+needle.size())) ++result;
    return result;
  };
  for (const auto& text : {
      "namespace n {}\nnamespace n {}\nusing namespace n;\n",
      "namespace n { int live; }\nnamespace n {}\nusing namespace n;\n",
      "namespace n {}\nnamespace n { int live; }\nusing namespace n;\n"}) {
    CHECK_EQ(count(render(source(text),{}).provenance.text,"namespace n {"),1u);
  }
  CHECK_EQ(render(source("namespace n {}\nnamespace n {}\n"),{}).provenance.text,"");
  const auto text = "namespace n { namespace inner {} }\nnamespace n { int live; }\nusing namespace n::inner;\n";
  const auto out = render(source(text),{}).provenance.text;
  CHECK_EQ(count(out,"namespace n {"),2u);
  CHECK(out.find("namespace inner") != out.npos);
  const auto beforeUse = "namespace n {}\nusing namespace n;\nnamespace n { int live; }\n";
  CHECK_EQ(count(render(source(beforeUse),{}).provenance.text,"namespace n {"),2u);
  const auto nested = "namespace n { namespace inner {} }\nnamespace n { namespace inner {} }\n"
      "using namespace n::inner;\n";
  CHECK_EQ(count(render(source(nested),{}).provenance.text,"namespace n {"),1u);
  const auto different = "namespace n { namespace unused {} }\nnamespace n { namespace inner {} }\n"
      "using namespace n::inner;\n";
  CHECK_EQ(count(render(source(different),{}).provenance.text,"namespace n {"),1u);
}
TEST(render_anonymous_inline_and_linkage_shells) {
  for (const auto& text : {
      "namespace { int dead; }\n",
      "inline namespace version { int dead; }\n",
      "extern \"C\" { int dead; }\n",
      "extern \"C++\" { namespace outer { int dead; } }\n",
      "namespace outer { extern \"C\" { namespace inner { int dead; } } }\n"}) {
    auto m = source(text);
    const auto dead = m.text.find("int dead;");
    CHECK_EQ(render(m,{{dead,dead+9}}).provenance.text,"");
  }
  for (const auto& text : {
      "namespace { namespace inner {} }\nusing namespace inner;\n",
      "inline namespace version { namespace inner {} }\nusing namespace inner;\n",
      "extern \"C\" { namespace inner {} }\nusing namespace inner;\n",
      "inline namespace version {}\nnamespace alias = version;\n",
      "extern \"C\" { int live; }\n",
      "namespace outer { extern \"C\" {\n#define X 1\n} }\n"}) {
    const auto out = render(source(text),{}).provenance.text;
    CHECK(out.find("#line 1 \"a.cpp\"\n") == 0);
    CHECK(out.find(text) != out.npos);
  }
}
TEST(render_inline_namespace_reopening_preserves_implicit_lookup) {
  for (const auto& text : {
      "inline namespace version {}\nnamespace version { int live; }\nint x = live;\n",
      "inline namespace version {}\nnamespace version { namespace inner {} }\nusing namespace inner;\n"}) {
    const auto out = render(source(text),{}).provenance.text;
    CHECK(out.find("inline namespace version") != out.npos);
    CHECK(out.find(text) != out.npos);
  }
}
TEST(render_without_line_markers_and_invariant) {
  auto m = source("int dead;\nint a;\nint gone;\n#define X 1\nint b;\n");
  const auto out = render(m,{{0,9},{m.text.find("int gone"),m.text.find("int gone")+9}});
  CHECK_EQ(out.provenance.text,"#line 2 \"a.cpp\"\nint a;\n#line 4\n#define X 1\nint b;\n");
  const auto unmarked = take(withoutLineMarkers(out));
  CHECK_EQ(unmarked,"int a;\n#define X 1\nint b;\n");
  CHECK(unmarkedInvariant(out.provenance.text,unmarked).ok());
  // An independent re-lex rejects glued or lost tokens, lost directive
  // boundaries and any remaining line directive or GNU linemarker.
  for (const auto& corrupt : {std::string("int a;\n#define X 1 int b;\n"), std::string("int a;#define X 1\nint b;\n"),
                              std::string("int a;\n#define X 1\nint\n"), std::string("#line 2\nint a;\n#define X 1\nint b;\n"),
                              std::string("# 2 \"a.cpp\"\nint a;\n#define X 1\nint b;\n"), std::string("inta;\n#define X 1\nint b;\n")}) {
    auto check = unmarkedInvariant(out.provenance.text,corrupt); CHECK(!check.ok());
  }
  auto bad = out; bad.provenance.generatedMarkers.front().end -= 1; // Not a whole line.
  CHECK(!withoutLineMarkers(bad).ok());
  bad = out; bad.provenance.generatedMarkers.front() = {16,22}; // Not a marker.
  CHECK(!withoutLineMarkers(bad).ok());
  // Rendering maps position uses like other offset provenance.
  m.positionUses = {{m.text.find("int b"),"__LINE__ expanded"}};
  const auto mapped = render(m,{{0,9}});
  CHECK_EQ(mapped.provenance.positionUses.front().offset,mapped.provenance.text.find("int b"));
}
TEST(render_line_marker_observations) {
  auto m = source("int a = 1;\n#pragma GCC diagnostic warning \"-Wall\"\nint b = 2;\n",10,"lib/x.hpp");
  CHECK(lineMarkerObservations(m,{"g++","-O2","-Wall","-Wno-error"}).empty());
  m.positionUses = {{m.text.find("2;"),"__LINE__ expanded"},{m.text.find("2;"),"__LINE__ expanded"}};
  CHECK_EQ(lineMarkerObservations(m,{}),(std::vector<std::string>{"__LINE__ expanded at lib/x.hpp:12"}));
  m.positionUses.clear();
  PruneMacroExpansion x; x.offset = m.text.find("1;"); x.name = "__FILE__"; m.expansions = {x};
  CHECK_EQ(lineMarkerObservations(m,{}),(std::vector<std::string>{"__FILE__ expanded at lib/x.hpp:10"}));
  x.name = "__COUNTER__"; m.expansions = {x}; // Not a position.
  CHECK(lineMarkerObservations(m,{}).empty());
  CHECK_EQ(lineMarkerObservations(m,{"g++","-Werror=misleading-indentation"}),
           (std::vector<std::string>{"-Werror=misleading-indentation makes warnings errors"}));
  auto p = source("#pragma GCC diagnostic error \"-Wmisleading-indentation\"\nint a;\n");
  CHECK_EQ(lineMarkerObservations(p,{}),(std::vector<std::string>{"diagnostic error pragma at a.cpp:1"}));
  p.lineMap[0].reset(); // A generated line has no original location.
  CHECK_EQ(lineMarkerObservations(p,{}),(std::vector<std::string>{"diagnostic error pragma at <generated>:1"}));
}
