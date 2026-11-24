// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.h"
#include "core/render/minify.h"
#include "core/command/options.h"
#include "core/names/names.h"
using namespace distill;
namespace {
template<class T> T take(Result<T> r) { if (!r.ok()) throw std::runtime_error(r.error().message); return std::move(r.value()); }
PruningModel model() { PruningModel m; m.minify = true; m.libraryMemberNamesComputed = true; return m; }
}
TEST(minify_cli_implies_render_flags_and_a15_is_independent) {
  const auto o = take(parseDistillOptions({"--minify","--no-assume=library-spelled-names","main.cpp"}));
  CHECK(o.minify && o.stripComments && o.noLineMarkers && o.compactWhitespace);
  CHECK(!o.assumeLibrarySpelledNames); CHECK(o.assumeLibrarySpelledMembers);
  CHECK(!parseDistillOptions({"--minify","--judge-probe","codeforces"}).ok());
  CHECK(distillHelp().find("--minify") != std::string::npos);
}
TEST(minify_frequency_injective_map_and_library_exclusions) {
  auto m = model(); m.libraryMemberNames = {"size","value_type","begin","a"};
  const std::string text = "int longestName; int otherName; int size; int value_type; int begin; int main(){longestName=longestName+otherName;return longestName;}\n";
  const auto out = take(minifySource(text,m));
  CHECK_EQ(out.renames.at("longestName"),"b"); CHECK_EQ(out.renames.at("otherName"),"c");
  CHECK(!out.renames.contains("main")); CHECK(!out.renames.contains("size"));
  CHECK(!out.renames.contains("value_type")); CHECK(!out.renames.contains("begin"));
  CHECK(take(minifyInvariant(text,out.text,out.renames)));
  CHECK_EQ(take(minifySource(text,m)).text,out.text);
}
TEST(minify_macro_literal_attribute_pragma_and_adl_exclusions) {
  auto m = model(); m.minifyExcludedNames = {"cFunction","systemParameter"};
  const std::string text = "#define STR(parameter) #parameter\n#define BODY(parameter) longLocal+parameter\n#pragma GCC diagnostic ignored \"longDiagnostic\"\n"
      "[[vendor::longAttribute]] int longLocal; int cFunction(); int systemParameter; int swap; int begin; int _Reserved; int has__reserved;\n"
      "auto s=STR(longArgument + longArgument); auto lit=\"longLiteral\"; auto hex=\"\\x6c\\x6f\\x6e\\x67\\x45\\x73\\x63\\x61\\x70\\x65\";\n"
      "int longLiteral,longEscape,longArgument,longDiagnostic; auto udl=1.e10_suffix; auto str=\"a\"_stringSuffix;\n"
      "long double operator\"\" _suffix(long double); long double operator\"\" _unusedSuffix(long double); int _stringSuffix; int longRenamed;\n";
  const auto out = take(minifySource(text,m));
  for (const auto* name : {"STR","BODY","parameter","cFunction","systemParameter","swap","begin","_Reserved","has__reserved","vendor","longAttribute","longLiteral","longEscape","longArgument","longDiagnostic","_suffix","_unusedSuffix","_stringSuffix"}) CHECK(!out.renames.contains(name));
  CHECK(out.renames.contains("longLocal")); CHECK(out.renames.contains("longRenamed"));
  CHECK(out.text.find("longArgument + longArgument") != out.text.npos);
}
TEST(minify_t1_lexer_edges_and_literal_bytes) {
  auto m = model(); m.assumeLibrarySpelledNames = false;
  const std::string text = "#define OBJ (1)\n#define FN(x) x\n"
      "int main() { a+ +b; x- -y; a/ *p; 1.e+ 1; 1e + 2; a < ::X > y; a > > b; a >> b; . . .; u8 \"x\";\n"
      "auto raw = u8R\"tag( raw text  \n  \\ \n)tag\"; auto suffix=\"x\"_tag; }\n";
  const auto out = take(minifySource(text,m));
  CHECK(out.renames.empty()); CHECK(take(minifyInvariant(text,out.text,{})));
  for (const auto* s : {"a+ +b","x- -y","a/ *p","1.e+ 1","1e +2","a< ::X>y","a> >b","a>>b","u8 \"x\"","\"x\"_tag"," raw text  \n  \\ \n"}) CHECK(out.text.find(s) != out.text.npos);
  CHECK(out.text.find("#define OBJ (1)\n#define FN(x)x\n") != out.text.npos);
}
TEST(minify_observable_names_refuse_t2_but_t1_runs) {
  for (const auto* name : {"typeid","__func__","__FUNCTION__","__PRETTY_FUNCTION__","source_location","__builtin_FUNCTION"}) {
    auto m = model(); const auto out = take(minifySource("int longestName; "+std::string(name)+";\n",m));
    CHECK(out.renames.empty()); CHECK(out.namesKept.find(name) != out.namesKept.npos);
  }
  auto m = model(); m.minifyNamesRefused = "observable names: typeid";
  CHECK(take(minifySource("int longestName;\n",m)).renames.empty());
  m = model(); m.libraryMemberNamesComputed = false;
  CHECK(!minifySource("int longestName;",m).ok());
  m.assumeLibrarySpelledNames = false;
  CHECK(minifySource("int longestName;",m).ok());
}
TEST(minify_invariant_rejects_broken_rename_directives_literals_and_macro_kind) {
  CHECK(!minifyInvariant("int longer; longer++;", "int a; longer++;",{{"longer","a"}}).ok());
  CHECK(!minifyInvariant("int longer;", "int b;",{{"longer","a"}}).ok());
  CHECK(!minifyInvariant("#define M x\nint y;", "#define M x int y;",{}).ok());
  CHECK(!minifyInvariant("#define M (x)\n", "#define M(x)\n",{}).ok());
  CHECK(!minifyInvariant("auto x=R\"(raw  text)\";", "auto x=R\"(raw text)\";",{}).ok());
  CHECK(!minifyInvariant("a+ +b;", "a++b;",{}).ok());
  CHECK(!minifyInvariant("first second", "a a",{{"first","a"},{"second","a"}}).ok());
  CHECK(!minifyInvariant("\"x\" _suffix", "\"x\"_suffix",{}).ok());
}

TEST(minify_rename_can_introduce_a_literal_prefix) {
  auto m = model();
  for (char c = 'a'; c <= 'z'; ++c) m.libraryMemberNames.insert(std::string(1,c));
  for (char c = 'A'; c < 'L'; ++c) m.libraryMemberNames.insert(std::string(1,c));
  const std::string text = "#define M longIdentifier\"literal\"\n";
  const auto out = take(minifySource(text,m));
  CHECK_EQ(out.renames.at("longIdentifier"),"L");
  CHECK(out.text.find("L \"literal\"") != out.text.npos);
  CHECK(take(minifyInvariant(text,out.text,out.renames)));
}

TEST(minify_object_macro_separator_invariant) {
  auto m = model();
  const std::string text = "#define TEXT \"x\"\n#define EMPTY\n#define SYMBOL +\nint main(){return TEXT[0]!='x';}\n";
  const auto out = take(minifySource(text,m));
  CHECK(out.text.find("#define TEXT \"x\"") != out.text.npos);
  CHECK(out.text.find("#define SYMBOL +") != out.text.npos);
  CHECK(!minifyInvariant("#define TEXT \"x\"\n", "#define TEXT\"x\"\n",{}).ok());
  CHECK(!minifyInvariant("#define TEXT x\n", "#defineTEXT x\n",{}).ok());
  CHECK(!minifyInvariant("#define N x\n", "#define Nx\n",{}).ok());
}
TEST(minify_sensitive_ranges_stop_at_directives_and_unknown_extents_refuse) {
  auto m = model();
  for (const auto* text : {
      "#define S(x) #x\n#define OPEN S(\nauto first=OPEN a + b);auto second=OPEN a + b);",
      "#define UNUSED )\nint main(){return 0;}",
      "#define S(x) #x\nS\n#define OTHER 1\n(a + b)",
      "#define S(x) #x\nS(a +\n#define OTHER 1\nb)"}) CHECK(!minifySource(text,m).ok());
  const std::string text = "#define S(x) #x\nauto first=S(a + b);\n#define OTHER 1\nauto second=S(a + b);";
  const auto out = take(minifySource(text,m));
  CHECK(out.text.find("S(a + b)") != out.text.npos);
  CHECK(out.text.rfind("S(a + b)") != out.text.find("S(a + b)"));
  m.text = text;
  PruneMacroExpansion e; e.offset = text.find("S(a"); e.name = "S"; e.hashOperator = true;
  m.expansions = {e}; CHECK(!minifySource(text,m).ok());
  e.invocationEnd = text.find(");")+1; m.expansions = {e};
  CHECK(minifySource(text,m).ok());
  CHECK(!minifySource(text+" ",m).ok()); // Stale provenance is never used.
}
TEST(minify_compiler_protocol_and_generated_names) {
  auto m = model();
  const auto& names = minifyCompilerNames();
  for (const auto* name : {"promise_type","get_return_object","get_return_object_on_allocation_failure",
      "initial_suspend","final_suspend","return_void","return_value","yield_value","unhandled_exception",
      "await_transform","await_ready","await_suspend","await_resume","from_promise","from_address","address","resume","destroy","done",
      "std","tuple_size","tuple_element","value","type","get","begin","end","size",
      "strong_ordering","weak_ordering","partial_ordering","initializer_list",
      "linux","unix","i386","i686","sun","sparc","mips","vax","BSD","M68020",
      "alloca","printf","strlen","ffs"}) CHECK(names.contains(name));
  std::string text;
  for (const auto& name : names) text += "int " + name + ";";
  const auto out = take(minifySource(text,m)); CHECK(out.renames.empty());
  // Make the allocator reach the three-character names without a huge source.
  // Both compiler builtin 'ffs' and language protocol 'get' lie in this span.
  constexpr std::string_view first = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  constexpr std::string_view more = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  for (char a : first) {
    m.minifyExcludedNames.insert(std::string(1,a));
    for (char b : more) m.minifyExcludedNames.insert(std::string{a,b});
  }
  for (char b : more) for (char c : more) m.minifyExcludedNames.insert(std::string{'a',b,c});
  // Judge predefines need not be queried or spelled in the source.
  m.judgeInitial["baa"] = {MacroState::known,Macro{false,{},false,"1"},{}};
  const auto allocated = take(minifySource("int longUserIdentifier;",m));
  CHECK_EQ(allocated.renames.at("longUserIdentifier"),"bab");
  // Start just before 'ffs'; blocked compiler names are skipped by allocation.
  for (char a : first) {
    if (a == 'f') break;
    for (char b : more) for (char c : more) m.minifyExcludedNames.insert(std::string{a,b,c});
  }
  for (char b : more) {
    if (b == 'f') break;
    for (char c : more) m.minifyExcludedNames.insert(std::string{'f',b,c});
  }
  for (char c : more) { if (c == 's') break; m.minifyExcludedNames.insert(std::string{'f','f',c}); }
  CHECK_EQ(take(minifySource("int longUserIdentifier;",m)).renames.at("longUserIdentifier"),"fft");
  for (char a : first) {
    if (a == 's') break;
    for (char b : more) for (char c : more) m.minifyExcludedNames.insert(std::string{a,b,c});
  }
  for (char b : more) {
    if (b == 'u') break;
    for (char c : more) m.minifyExcludedNames.insert(std::string{'s',b,c});
  }
  for (char c : more) { if (c == 'n') break; m.minifyExcludedNames.insert(std::string{'s','u',c}); }
  CHECK_EQ(take(minifySource("int longUserIdentifier;",m)).renames.at("longUserIdentifier"),"suo");
}
