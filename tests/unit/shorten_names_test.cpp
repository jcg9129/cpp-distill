// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.h"
#include "core/render/shorten_names.h"
#include "core/command/options.h"
#include "core/names/names.h"
using namespace distill;
namespace {
template<class T> T take(Result<T> r) { if (!r.ok()) throw std::runtime_error(r.error().message); return std::move(r.value()); }
PruningModel model() { PruningModel m; m.shortenNames = true; m.libraryMemberNamesComputed = true; return m; }
}
TEST(shortenNames_cli_implies_render_flags_and_a15_is_independent) {
  const auto o = take(parseDistillOptions({"--shorten-names","--no-assume=library-spelled-names","main.cpp"}));
  CHECK(o.shortenNames && o.stripComments && o.noLineMarkers && o.compactWhitespace);
  CHECK(!o.assumeLibrarySpelledNames); CHECK(o.assumeLibrarySpelledMembers);
  CHECK(!parseDistillOptions({"--shorten-names","--judge-probe","codeforces"}).ok());
  CHECK(distillHelp().find("--shorten-names") != std::string::npos);
  CHECK(!parseDistillOptions({"--minify","main.cpp"}).ok());
  CHECK(distillHelp().find("--minify") == std::string::npos);
}
TEST(shortenNames_frequency_injective_map_and_library_exclusions) {
  auto m = model(); m.libraryMemberNames = {"size","value_type","begin","a"};
  const std::string text = "int longestName; int otherName; int size; int value_type; int begin; int main(){longestName=longestName+otherName;return longestName;}\n";
  const auto out = take(shortenNamesSource(text,m));
  CHECK_EQ(out.renames.at("longestName"),"lngsNm"); CHECK_EQ(out.renames.at("otherName"),"otherNm");
  CHECK(!out.renames.contains("main")); CHECK(!out.renames.contains("size"));
  CHECK(!out.renames.contains("value_type")); CHECK(!out.renames.contains("begin"));
  CHECK(take(shortenNamesInvariant(text,out.text,out.renames)));
  CHECK_EQ(take(shortenNamesSource(text,m)).text,out.text);
}
TEST(shortenNames_macro_literal_attribute_pragma_and_adl_exclusions) {
  auto m = model(); m.shorteningExcludedNames = {"cFunction","systemParameter"};
  const std::string text = "#define STR(parameter) #parameter\n#define BODY(parameter) longLocal+parameter\n#pragma GCC diagnostic ignored \"longDiagnostic\"\n"
      "[[vendor::longAttribute]] int longLocal; int cFunction(); int systemParameter; int swap; int begin; int _Reserved; int has__reserved;\n"
      "auto s=STR(longArgument + longArgument); auto lit=\"longLiteral\"; auto hex=\"\\x6c\\x6f\\x6e\\x67\\x45\\x73\\x63\\x61\\x70\\x65\";\n"
      "int longLiteral,longEscape,longArgument,longDiagnostic; auto udl=1.e10_suffix; auto str=\"a\"_stringSuffix;\n"
      "long double operator\"\" _suffix(long double); long double operator\"\" _unusedSuffix(long double); int _stringSuffix; int longRenamed;\n";
  const auto out = take(shortenNamesSource(text,m));
  for (const auto* name : {"STR","BODY","parameter","cFunction","systemParameter","swap","begin","_Reserved","has__reserved","vendor","longAttribute","longLiteral","longEscape","longArgument","longDiagnostic","_suffix","_unusedSuffix","_stringSuffix"}) CHECK(!out.renames.contains(name));
  CHECK(out.renames.contains("longLocal")); CHECK(out.renames.contains("longRenamed"));
  CHECK(out.text.find("longArgument + longArgument") != out.text.npos);
}
TEST(shorten_names_preserves_all_layout_and_literal_bytes) {
  auto m = model();
  const std::string text = "#define OBJ (1)\n#define FN(x) x\n"
      "int read_stream_magnitude = 0;\nread_stream_magnitude += 1;\n"
      "auto raw = R\"tag( raw  text\n  more text)tag\";\n";
  const auto out = take(shortenNamesSource(text,m));
  std::string expected = text;
  for (auto pos = expected.find("read_stream_magnitude"); pos != expected.npos;
       pos = expected.find("read_stream_magnitude",pos+12))
    expected.replace(pos,21,"rd_strm_mgnt");
  CHECK_EQ(out.text,expected);
  CHECK(take(shortenNamesInvariant(text,out.text,out.renames)));
  m.assumeLibrarySpelledNames = false;
  CHECK_EQ(take(shortenNamesSource(text,m)).text,text);
}
TEST(shorten_names_observable_names_keep_compact_bytes) {
  for (const auto* name : {"typeid","__func__","__FUNCTION__","__PRETTY_FUNCTION__","source_location","__builtin_FUNCTION",
      "__builtin_dump_struct","__builtin_FILE","__builtin_LINE","__builtin_COLUMN","__builtin_FILE_NAME",
      "__builtin_FUNCSIG","__builtin_source_location","__builtin_sycl_unique_stable_name",
      "__builtin_get_device_side_mangled_name","L__FUNCTION__","L__FUNCSIG__"}) {
    auto m = model(); const auto out = take(shortenNamesSource("int longestName; "+std::string(name)+";\n",m));
    CHECK(out.renames.empty()); CHECK_EQ(out.namesKept,"observable names: " + std::string(name));
  }
  auto m = model(); m.shorteningRefused = "observable names: typeid";
  CHECK(take(shortenNamesSource("int longestName;\n",m)).renames.empty());
  m = model(); m.libraryMemberNamesComputed = false;
  CHECK(!shortenNamesSource("int longestName;",m).ok());
  m.assumeLibrarySpelledNames = false;
  CHECK(shortenNamesSource("int longestName;",m).ok());
}
TEST(shortenNames_invariant_rejects_broken_rename_directives_literals_and_macro_kind) {
  CHECK(!shortenNamesInvariant("int longer; longer++;", "int a; longer++;",{{"longer","a"}}).ok());
  CHECK(!shortenNamesInvariant("int longer;", "int b;",{{"longer","a"}}).ok());
  CHECK(!shortenNamesInvariant("#define M x\nint y;", "#define M x int y;",{}).ok());
  CHECK(!shortenNamesInvariant("#define M (x)\n", "#define M(x)\n",{}).ok());
  CHECK(!shortenNamesInvariant("auto x=R\"(raw  text)\";", "auto x=R\"(raw text)\";",{}).ok());
  CHECK(!shortenNamesInvariant("a+ +b;", "a++b;",{}).ok());
  CHECK(!shortenNamesInvariant("first second", "a a",{{"first","a"},{"second","a"}}).ok());
  CHECK(!shortenNamesInvariant("\"x\" _suffix", "\"x\"_suffix",{}).ok());
}

TEST(shortenNames_object_macro_separator_invariant) {
  auto m = model();
  const std::string text = "#define TEXT \"x\"\n#define EMPTY\n#define SYMBOL +\nint main(){return TEXT[0]!='x';}\n";
  const auto out = take(shortenNamesSource(text,m));
  CHECK(out.text.find("#define TEXT \"x\"") != out.text.npos);
  CHECK(out.text.find("#define SYMBOL +") != out.text.npos);
  CHECK(!shortenNamesInvariant("#define TEXT \"x\"\n", "#define TEXT\"x\"\n",{}).ok());
  CHECK(!shortenNamesInvariant("#define TEXT x\n", "#defineTEXT x\n",{}).ok());
  CHECK(!shortenNamesInvariant("#define N x\n", "#define Nx\n",{}).ok());
}
TEST(shorten_names_sensitive_arguments_and_unbalanced_replacements) {
  auto m = model();
  const std::string text = "#define S(x) #x\n#define UNUSED )\n"
      "auto first=S(longArgument + longArgument);\n#define OTHER 1\n"
      "int longArgument; int read_stream_magnitude;\n";
  const auto out = take(shortenNamesSource(text,m));
  CHECK(!out.renames.contains("longArgument"));
  CHECK_EQ(out.renames.at("read_stream_magnitude"),"rd_strm_mgnt");
  CHECK(out.text.find("#define UNUSED )\n") != out.text.npos);
  CHECK(out.text.find("S(longArgument + longArgument)") != out.text.npos);
}
TEST(shortenNames_compiler_protocol_and_generated_names) {
  auto m = model();
  const auto& names = shortenNamesCompilerNames();
  for (const auto* name : {"promise_type","get_return_object","get_return_object_on_allocation_failure",
      "initial_suspend","final_suspend","return_void","return_value","yield_value","unhandled_exception",
      "await_transform","await_ready","await_suspend","await_resume","from_promise","from_address","address","resume","destroy","done",
      "std","tuple_size","tuple_element","value","type","get","begin","end","size",
      "strong_ordering","weak_ordering","partial_ordering","initializer_list",
      "linux","unix","i386","i686","sun","sparc","mips","vax","BSD","M68020",
      "alloca","printf","strlen","ffs"}) CHECK(names.contains(name));
  std::string text;
  for (const auto& name : names) text += "int " + name + ";";
  const auto out = take(shortenNamesSource(text,m)); CHECK(out.renames.empty());
  // These blocked candidates are absent from the source itself.
  m.judgeInitial["mgnt"] = {MacroState::known,Macro{false,{},false,"1"},{}};
  CHECK_EQ(take(shortenNamesSource("int magnitude;",m)).renames.at("magnitude"),"magni");
  m.shorteningExcludedNames.insert("magni");
  CHECK_EQ(take(shortenNamesSource("int magnitude;",m)).renames.at("magnitude"),"magnit");
  // Vowel dropping would generate keywords and compiler/target names.
  const auto blocked = take(shortenNamesSource("int integer,internet,feefas;",m));
  CHECK_EQ(blocked.renames.at("integer"),"inte");
  CHECK_EQ(blocked.renames.at("feefas"),"feef");
  for (const auto& [old,name] : blocked.renames) {
    (void)old; CHECK(name != "int"); CHECK(name != "alloca"); CHECK(name != "linux");
  }
}
TEST(shorten_names_readable_words_case_digits_and_profitability) {
  auto m = model();
  const std::string text = "int read_stream_magnitude,isUnsignedIntegral,READ_STREAM_MAGNITUDE;"
      "int uint32,float64,uint128,Graph,Value,foo,Read,ValueGraph,GraphEngine;"
      "int uint32_magnitude,float64Magnitude,HTTPServer,vertices_index;";
  const auto out = take(shortenNamesSource(text,m));
  CHECK_EQ(out.renames.at("read_stream_magnitude"),"rd_strm_mgnt");
  CHECK_EQ(out.renames.at("isUnsignedIntegral"),"isUnsgIntg");
  CHECK_EQ(out.renames.at("READ_STREAM_MAGNITUDE"),"RD_STRM_MGNT");
  CHECK_EQ(out.renames.at("Value"),"Val");
  CHECK_EQ(out.renames.at("ValueGraph"),"ValGraph");
  CHECK_EQ(out.renames.at("uint32_magnitude"),"uint32_mgnt");
  CHECK_EQ(out.renames.at("float64Magnitude"),"float64Mgnt");
  CHECK_EQ(out.renames.at("HTTPServer"),"HTTPSrvr");
  CHECK_EQ(out.renames.at("GraphEngine"),"GraphEngn");
  CHECK_EQ(out.renames.at("vertices_index"),"vtxs_idx");
  for (const auto* name : {"uint32","float64","uint128","Graph","foo","Read"})
    CHECK(!out.renames.contains(name));
  CHECK_EQ(take(shortenNamesSource(text,m)).renames,out.renames);
}
TEST(shorten_names_collisions_lengthen_words_then_append_digits) {
  auto m = model();
  // Base candidates collide with another source spelling and a kept spelling.
  m.shorteningExcludedNames = {"rd_strea_mgnt"};
  const std::string text = "int rd_strm_mgnt,rd_stream_magni,rd_stream_magnit,rd_stream_magnitude;"
      "int read_stream_magnitude,magnitudes,magnitude;";
  const auto out = take(shortenNamesSource(text,m));
  CHECK_EQ(out.renames.at("read_stream_magnitude"),"read_strm_mgnt");
  CHECK(out.renames.at("magnitude") != out.renames.at("magnitudes"));
  CHECK(take(shortenNamesInvariant(text,out.text,out.renames)));
  // Every lengthened candidate is blocked; use a digit on the readable base.
  m.shorteningExcludedNames = {"mgnt","magni","magnit","magnitu","mgnt1"};
  CHECK_EQ(take(shortenNamesSource("int magnitude;",m)).renames.at("magnitude"),"mgnt2");
  // No one-character savings, even when collision lengthening reaches the end.
  m.shorteningExcludedNames = {"cnt","cnt1"};
  CHECK(take(shortenNamesSource("int count;",m)).renames.empty());
}
TEST(shorten_names_new_names_are_injective_and_all_originals_reserved) {
  auto m = model();
  const std::string text = "int comparator,compare,cmp; comparator=comparator+compare;";
  const auto out = take(shortenNamesSource(text,m));
  CHECK_EQ(out.renames.at("comparator"),"comp");
  CHECK_EQ(out.renames.at("compare"),"compa");
  CHECK(!out.renames.contains("cmp"));
  CHECK(take(shortenNamesInvariant(text,out.text,out.renames)));
}

TEST(shorten_names_spliced_identifiers_keep_their_physical_lines) {
  auto m = model();
  const std::string text = "int magni\\\nmagnitude;\nmagnimagnitude = 1;\n";
  const auto out = take(shortenNamesSource(text,m));
  CHECK(out.renames.empty()); CHECK_EQ(out.text,text);
}

TEST(shorten_names_literal_prefixes_are_never_allocated) {
  const std::set<std::string> prefixes = {"L","u","U","u8","R","LR","uR","UR","u8R"};
  for (const auto* original : {"Looooo","uaaaaa","Uaaaaa","Raaaaa","LoooooRaaaaa",
       "uaaaaaRaaaaa","UaaaaaRaaaaa","u8Raaaaa"}) {
    const auto out = take(shortenNamesSource("int " + std::string(original) + ";",model()));
    CHECK(out.renames.contains(original));
    CHECK(!prefixes.contains(out.renames.at(original)));
  }
  auto m = model();
  m.shorteningExcludedNames = {"ua","uaa","uaaa","u1","u2","u3","u4","u5","u6","u7"};
  CHECK_EQ(take(shortenNamesSource("int uaaaaa;",m)).renames.at("uaaaaa"),"u9");
  const std::string text = "#define IGNORE(...) 0\nint main(){return IGNORE(Looooo\"x\");}\n";
  const auto out = take(shortenNamesSource(text,model()));
  CHECK_EQ(out.renames.at("Looooo"),"Lo");
  CHECK(take(shortenNamesInvariant(text,out.text,out.renames)));
  // Keep the independent invariant even though the allocator avoids prefixes.
  CHECK(!shortenNamesInvariant("Looooo\"x\"", "L\"x\"",{{"Looooo","L"}}).ok());
}

TEST(shorten_names_surviving_conditionals_keep_every_byte) {
  for (const auto* directive : {"if 0","ifdef ENABLE","ifndef ENABLE","elif 0","elifdef ENABLE",
       "elifndef ENABLE","else","endif"}) {
    const std::string text = "#" + std::string(directive) + "\nint long_identifier;\n";
    const auto out = take(shortenNamesSource(text,model()));
    CHECK(out.renames.empty()); CHECK_EQ(out.text,text);
    CHECK(out.namesKept.starts_with("surviving conditional directive: #"));
  }
  const std::string text = "#define STR(x) #x\n#define OPEN_STRINGIZE STR(\n#if 0\n"
      "const char* text=OPEN_STRINGIZE long_identifier);\n#endif\n"
      "int long_identifier=7; int main(){return long_identifier;}\n";
  const auto out = take(shortenNamesSource(text,model()));
  CHECK_EQ(out.text,text); CHECK(out.renames.empty());
  CHECK_EQ(out.namesKept,"surviving conditional directive: #if");
}

TEST(shorten_names_allocator_skips_keywords_without_dialect_or_source_inventory) {
  auto m = model();
  // None of these candidate keywords occurs in the input or worker inventory.
  auto out = take(shortenNamesSource("int aenode,aseme,typeoffoo,restrictxx;",m));
  CHECK_EQ(out.renames.at("aenode"),"aeno");
  CHECK(!out.renames.contains("aseme"));
  CHECK(out.renames.at("typeoffoo") != "typeof");
  m.shorteningExcludedNames = {"typf","typeo","rstr","restr","restri","restric"};
  out = take(shortenNamesSource("int typeoffoo,restrictxx;",m));
  CHECK_EQ(out.renames.at("typeoffoo"),"typeoff");
  CHECK_EQ(out.renames.at("restrictxx"),"rstr1");
  m.shorteningExcludedNames = {"typf_unql","typeof_unql","typeof_unqua","hlf"};
  out = take(shortenNamesSource("int typeof_unqualooo,halfooo;",m));
  CHECK_EQ(out.renames.at("typeof_unqualooo"),"typeof_unqualo");
  CHECK_EQ(out.renames.at("halfooo"),"halfo");
  m.shorteningExcludedNames = {"_cdcl"};
  CHECK_EQ(take(shortenNamesSource("int _cdeclxx;",m)).renames.at("_cdeclxx"),"_cdcl1");
  for (const auto* keyword : {"and","and_eq","asm","bitand","bitor","compl","not","not_eq",
       "or","or_eq","xor","xor_eq","typeof","typeof_unqual","restrict","char8_t",
       "consteval","constinit","co_await","co_return","co_yield","contract_assert"}) {
    const auto kept = take(shortenNamesSource("int " + std::string(keyword) + ";",model()));
    CHECK(kept.renames.empty());
  }
}

TEST(shorten_names_ascii_case_and_non_ascii_bytes) {
  const std::string text = "int INTEGER,ReadStreamMagnitude,\xc3\x89quantity_count;";
  const auto out = take(shortenNamesSource(text,model()));
  CHECK_EQ(out.renames.at("INTEGER"),"INT");
  CHECK_EQ(out.renames.at("ReadStreamMagnitude"),"RdStrmMgnt");
  CHECK(out.text.find("\xc3\x89") != out.text.npos);
  CHECK(take(shortenNamesInvariant(text,out.text,out.renames)));
}
