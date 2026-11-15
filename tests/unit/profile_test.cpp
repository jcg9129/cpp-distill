// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.h"
#include "core/profile/profile.h"
#include "core/profile/toml.h"
#include <cstdint>
#include <limits>
#include <tuple>
using namespace distill;
namespace {
std::filesystem::path profileFixture(const char* file) { return std::filesystem::path(DISTILL_PROFILE_FIXTURES) / file; }
std::string replace(std::string text, std::string_view before, std::string_view after) {
  auto pos = text.find(before);
  if (pos == text.npos) throw std::runtime_error("test replacement not found");
  text.replace(pos, before.size(), after);
  return text;
}
std::string allowProfile() {
  auto text = test::read(profileFixture("mingw.toml"));
  text = replace(text, "predefined = \"mingw-predefined.txt\"", "");
  return text;
}
Result<Profile> synthetic(std::string text, std::string snapshot = test::read(profileFixture("mingw-predefined.txt"))) {
  return parseProfile(text, "mingw.toml", [snapshot](const auto&) -> Result<std::string> { return snapshot; });
}
void badProfile(const std::string& text, std::string_view reason) {
  auto result = synthetic(text);
  CHECK(!result.ok());
  if (!result.ok()) {
    CHECK(result.error().message.starts_with("mingw.toml:"));
    CHECK(result.error().message.find(reason) != std::string::npos);
  }
}
}
TEST(profile_toml_accepted_strings_keys_comments_arrays_tables) {
  const auto result = parseProfileToml(R"toml(# comment
bare-key = "é # still string" # trailing comment
"quoted key" = 'literal \n " #'
'key2' = "\\ \" \n \t \u03b1 \U0001F600"
array = ["a", # comment
 'b',
]
empty = []
compiler = { family = "gcc", 'version' = '14.2' }
table = {}
)toml", "test");
  CHECK(result.ok()); if (!result.ok()) return;
  const auto& d = result.value();
  CHECK_EQ(std::get<std::string>(d.at("bare-key").value), "é # still string");
  CHECK_EQ(std::get<std::string>(d.at("quoted key").value), "literal \\n \" #");
  CHECK_EQ(std::get<std::string>(d.at("key2").value), "\\ \" \n \t α 😀");
  CHECK_EQ(std::get<TomlStrings>(d.at("array").value), (TomlStrings{"a", "b"}));
  CHECK(std::get<TomlStrings>(d.at("empty").value).empty());
  CHECK_EQ(std::get<TomlTable>(d.at("compiler").value).at("version"), "14.2");
  CHECK(std::get<TomlTable>(d.at("table").value).empty());
  CHECK_EQ(d.at("compiler").line, 9u);
  CHECK(parseProfileToml("\xef\xbb\xbf\"\" = ''\r\n\t# comment\r\nx = [\r\n\"z\",\r\n]\r\n", "bom").ok());
  CHECK(parseProfileToml("x='a'", "no-newline").ok());
}
TEST(profile_toml_booleans_and_integers) {
  auto r = parseProfileToml("a=true\nb=false\nx=+42\ny=-0\nz=-9223372036854775808\nw=9223372036854775807\n", "test");
  CHECK(r.ok()); if (!r.ok()) return;
  CHECK(std::get<bool>(r.value().at("a").value)); CHECK(!std::get<bool>(r.value().at("b").value));
  CHECK_EQ(std::get<std::int64_t>(r.value().at("x").value), 42);
  CHECK_EQ(std::get<std::int64_t>(r.value().at("y").value), 0);
  CHECK_EQ(std::get<std::int64_t>(r.value().at("z").value), std::numeric_limits<std::int64_t>::min());
  CHECK_EQ(std::get<std::int64_t>(r.value().at("w").value), std::numeric_limits<std::int64_t>::max());
}
TEST(profile_toml_integer_bases_and_separators) {
  auto r = parseProfileToml("a=0xDEAD_beef\nb=0o7_55\nc=0b1_01\nd=+1_000\ne=-9_223_372_036_854_775_808", "test");
  CHECK(r.ok()); if (!r.ok()) return;
  CHECK_EQ(std::get<std::int64_t>(r.value().at("a").value), 0xdeadbeefLL);
  CHECK_EQ(std::get<std::int64_t>(r.value().at("b").value), 0755);
  CHECK_EQ(std::get<std::int64_t>(r.value().at("c").value), 5);
  CHECK_EQ(std::get<std::int64_t>(r.value().at("d").value), 1000);
  CHECK_EQ(std::get<std::int64_t>(r.value().at("e").value), std::numeric_limits<std::int64_t>::min());
}
TEST(profile_toml_rejects_every_unsupported_form_and_malformed_syntax) {
  for (const auto* text : {"[x]", "[[x]]", "x.y=1", "x=2026-11-15", "x=1.2", "x=inf", "x=nan", "x=1e3", "x=0X12", "x=1__000", "x=1_", "x=_1", "x=0_1", "x=+0x12", "x=0o8", "x=0b2", "x=0x_FF", "x=0x", "x=0x8000000000000000", "x=01", "x=+", "x=", "x=TRUE", "x=null", "x=bare", "x 1", "=1", "x=1 x=2", "x=1\nx=2", "x=1\n'x'=2", "x=[1]", "x=[true]", "x=[['a']]", "x=['a' 'b']", "x=['a',,]", "x=['a'", "x={a='1', a='2'}", "x={a=1}", "x={a=true}", "x={a=['a']}", "x={a='1',}", "x={\na='1'}", "x={a '1'}", "x={a='1' b='2'}", "x={a='1'", "x='unterminated", "x=\"bad\\q\"", "x=\"bad\\r\"", "x=\"bad\\b\"", "x=\"bad\\f\"", "x=\"bad\\", "x=\"\\u12\"", "x=\"\\uQQQQ\"", "x=\"\\uD800\"", "x=\"\\U00110000\"", "x=\"\\UFFFFFFFF\"", "x=\"\"\"multi\"\"\"", "x='''multi'''", "x='a\nb'", "x=\"a\nb\"", "x=9223372036854775808", "x=-9223372036854775809"}) {
    auto r = parseProfileToml(text, "test"); CHECK(!r.ok());
    if (!r.ok()) CHECK(r.error().message.starts_with("test:"));
  }
}
TEST(profile_toml_utf8_controls_and_error_line_numbers) {
  for (const std::string& text : {std::string("x='\xff'"), std::string("#\xc0\xaf"), std::string("#\xed\xa0\x80"), std::string("#\xf4\x90\x80\x80"), std::string("#\xe2\x82"), std::string("#\x80"), std::string("#\x7f"), std::string("#\0", 2), std::string("#\r")}) CHECK(!parseProfileToml(text, "test").ok());
  const auto r = parseProfileToml("# first\n\nx = [\n 'a',\n 2\n]\n", "file");
  CHECK(!r.ok()); CHECK(r.error().message.starts_with("file:5:"));
  const auto u = parseProfileToml("# ok\n#\xff", "file"); CHECK(u.error().message.starts_with("file:2:"));
}
TEST(profile_loads_reproduced_and_approximate_fixtures) {
  auto r = loadProfile(profileFixture("mingw.toml")); CHECK(r.ok()); if (!r.ok()) return;
  const auto& p = r.value();
  CHECK(p.reproduced); CHECK_EQ(p.triple, "x86_64-w64-mingw32");
  CHECK_EQ(p.compiler.major, 14); CHECK_EQ(p.compiler.minor, 2); CHECK(!p.patchSpecified);
  CHECK_EQ(p.dialect.name, "c++23"); CHECK(!p.dialect.gnu);
  CHECK(p.predefined.at("FUNCTION").functionLike); CHECK(p.predefined.at("FUNCTION").variadic);
  CHECK_EQ(p.initialState("ONLINE_JUDGE").definition->replacement, "1");
  CHECK_EQ(p.initialState("ONLINE_JUDGE").provenance.front(), FactOrigin::commandOperation);
  CHECK_EQ(p.initialState("__absent_reserved").state, MacroState::undefined);
  CHECK_EQ(p.initialState("__absent_reserved").provenance.front(), FactOrigin::a4);
  CHECK_EQ(p.initialState("LOCAL").provenance.front(), FactOrigin::a5);
  CHECK_EQ(p.libraries, (std::vector<std::string>{"mingw-w64", "libstdc++"}));
  auto linux = loadProfile(profileFixture("linux.toml")); CHECK(linux.ok()); if (!linux.ok()) return;
  CHECK(!linux.value().reproduced); CHECK(linux.value().predefined.size() > 400);
  CHECK_EQ(linux.value().initialState("__aarch64__").state, MacroState::known);
  CHECK_EQ(linux.value().initialState("__aarch64__").provenance.back(), FactOrigin::allowList);
  CHECK_EQ(linux.value().initialState("__OPTIMIZE__").state, MacroState::unknown);
  CHECK_EQ(linux.value().initialState("__CHAR_UNSIGNED__").state, MacroState::known);
  CHECK_EQ(linux.value().initialState("__absent_reserved").state, MacroState::unknown);
  CHECK_EQ(linux.value().initialState("LOCAL").state, MacroState::undefined);
  CHECK_EQ(linux.value().initialState("EMPTY").definition->replacement, "");
}
TEST(profile_mode_requires_matching_build_and_complete_registry) {
  auto text = test::read(profileFixture("mingw.toml"));
  auto snapshot = test::read(profileFixture("mingw-predefined.txt"));
  for (const auto& other : {replace(text, "build = { package = \"synthetic:fixture=14.2.0-1\" }", ""),
                           replace(text, "synthetic:fixture=14.2.0-1", "other:build=1")}) {
    const auto inventory = other.find("build =") == std::string::npos ? replace(snapshot, "# reproduced: yes", "# reproduced: no") : snapshot;
    auto r = synthetic(other, inventory); CHECK(r.ok()); if (r.ok()) {
      CHECK(!r.value().reproduced);
      CHECK_EQ(r.value().initialState("__has_include").state, MacroState::unknown);
      CHECK_EQ(r.value().initialState("__GNUC__").state, MacroState::known);
    }
  }
  auto r = synthetic(text, replace(snapshot, "# reproduced: yes", "# reproduced: no"));
  CHECK(r.ok()); if (r.ok()) CHECK(!r.value().reproduced);
  auto unsupportedText = replace(text, "version = \"14.2\"", "version = \"14.9\"");
  auto unsupportedSnapshot = replace(snapshot, "# gcc-version: 14.2.0", "# gcc-version: 14.9.0");
  unsupportedSnapshot = replace(unsupportedSnapshot, "# builtin-registry: gcc-14.2", "# builtin-registry: none");
  unsupportedSnapshot = replace(unsupportedSnapshot, "#define __GNUC_MINOR__ 2", "#define __GNUC_MINOR__ 9");
  for (const auto& name : specialBuiltins({CompilerFamily::gcc, 14, 2, 0})) {
    unsupportedSnapshot = replace(unsupportedSnapshot, "# state " + name + " defined-unknown special-builtin", "");
    unsupportedSnapshot = replace(unsupportedSnapshot, "# probe " + name + " defined", "");
  }
  auto unsupported = synthetic(unsupportedText, unsupportedSnapshot); CHECK(unsupported.ok());
  if (unsupported.ok()) { CHECK(!unsupported.value().reproduced); CHECK_EQ(unsupported.value().initialState("__new_builtin").state, MacroState::unknown); }
  CHECK(specialBuiltins({CompilerFamily::gcc, 99, 1, 0}).empty());
  CHECK(!approximateAllowed("__AVX2__")); CHECK(!approximateAllowed("__OPTIMIZE__"));
  for (const auto* n : {"__GNUC__", "_WIN32", "__SIZEOF_INT__", "__INT_MAX__", "__CHAR_UNSIGNED__", "__cplusplus"}) CHECK(approximateAllowed(n));
}
TEST(profile_snapshot_compiler_is_approximate_and_cannot_claim_reproduction) {
  const auto original = test::read(profileFixture("mingw.toml"));
  const auto text = replace(original, "build = { package = \"synthetic:fixture=14.2.0-1\" }",
                            "snapshot_compiler = { package = \"msys2:mingw-w64-x86_64-gcc=14.2.0-3\" }");
  const auto snapshot = replace(test::read(profileFixture("mingw-predefined.txt")),
      "# build: package=synthetic:fixture=14.2.0-1", "# build: none\n# snapshot-compiler: package=msys2:mingw-w64-x86_64-gcc=14.2.0-3");
  auto r = synthetic(text, replace(snapshot, "# reproduced: yes", "# reproduced: no"));
  CHECK(r.ok()); if (!r.ok()) return;
  CHECK(!r.value().build); CHECK(r.value().snapshotCompiler.has_value()); CHECK(!r.value().reproduced);
  CHECK_EQ(r.value().snapshotMetadata.snapshotCompiler, "package=msys2:mingw-w64-x86_64-gcc=14.2.0-3");
  CHECK_EQ(r.value().initialState("__GNUC__").state, MacroState::known);
  CHECK_EQ(r.value().initialState("__has_include").state, MacroState::unknown);
  CHECK_EQ(r.value().initialState("__absent_reserved").state, MacroState::unknown);
  auto claimed = synthetic(text, snapshot); CHECK(!claimed.ok());
  if (!claimed.ok()) CHECK(claimed.error().message.find("requires build provenance") != std::string::npos);
  CHECK(!synthetic(replace(original, "build =", "snapshot_compiler =")).ok()); // header must record it
  CHECK(!synthetic(text, replace(snapshot, "gcc=14.2.0-3", "gcc=14.2.0-2")).ok());
  for (const auto* value : {"{}", "{package=''}", "{package='gcc'}", "{package='gcc=1 2'}", "{recipe='not-url'}", "{typo='x'}", "{package='gcc=1',recipe='https://x'}", "[]"})
    badProfile(allowProfile() + "\nsnapshot_compiler = " + value, "snapshot_compiler:");
}
TEST(profile_schema_required_unknown_types_and_values) {
  auto text = allowProfile(); auto doc = parseProfileToml(text, "test").value();
  for (const auto& [key, entry] : doc) {
    if (key == "build") continue;
    auto begin = text.find(key + " ="); auto end = text.find('\n', begin);
    auto missing = text; missing.erase(begin, end - begin); badProfile(missing, "required");
    auto wrong = text; wrong.replace(begin + key.size() + 3, end - (begin + key.size() + 3), "true"); badProfile(wrong, "wrong type");
    (void)entry;
  }
  for (const auto* extra : {"defines=[]", "library='glibc'", "allowed=[]", "typo=true"}) badProfile(text + "\n" + extra, "unknown key");
  for (auto [before, after, error] : {std::tuple{"name = \"mingw\"", "name = \"Bad_Name\"", "name:"},
       {"triple = \"x86_64-w64-mingw32\"", "triple = \"bad\"", "triple:"},
       {"family = \"gcc\"", "family = \"clang\"", "only gcc"},
       {"version = \"14.2\"", "version = \"14\"", "major.minor"},
       {"version = \"14.2\"", "version = \"999999999999999.2\"", "version"},
       {"std = \"c++23\"", "std = \"gnu++2b\"", "c++NN"},
       {"std = \"c++23\"", "std = \"c++99\"", "unsupported"},
       {"libraries = [\"mingw-w64\", \"libstdc++\"]", "libraries = ['bad']", "libraries:"}}) badProfile(replace(text, before, after), error);
  for (const auto* value : {"{}", "{package='x',recipe='https://x'}", "{typo='x'}", "{recipe='not-url'}"})
    badProfile(replace(text, "{ package = \"synthetic:fixture=14.2.0-1\" }", value), "build:");
  for (const auto* date : {"2026-02-29", "2026-04-31", "2026-13-01", "0000-01-01", "today"}) badProfile(replace(text, "2026-11-15", date), "updated:");
  for (const auto* source : {"[]", "['']", "['file:///x']", "['https://bad host/']"}) badProfile(replace(text, "[\"https://gcc.gnu.org/gcc-14/\"]", source), "sources:");
}
TEST(profile_flags_use_command_classifier_and_inert_cannot_bypass_rejection) {
  const auto base = allowProfile();
  for (const auto* flag : {"-std=gnu++23", "-includeevil", "-imacros", "-Wp,-DNAME", "-Xpreprocessor", "-Wl,x", "-Wa,x", "-Bpath", "--sysroot=x", "-specs=x", "-fplugin=x", "-c", "@response", "-oout", "-Iinc", "-lfoo"}) {
    auto text = replace(base, "flags = [\"-DONLINE_JUDGE\"]", "flags = [\"" + std::string(flag) + "\"]");
    CHECK(!synthetic(text + "\ninert = [\"" + std::string(flag) + "\"]").ok());
  }
  auto flags = replace(base, "flags = [\"-DONLINE_JUDGE\"]", "flags = ['-O2','-Wall','-fmodules','-fopenmp','-pthread','-march=native','-fconstexpr-depth=1024']");
  CHECK(!synthetic(flags).ok()); CHECK(synthetic(flags + "\ninert=['-fconstexpr-depth=1024']").ok());
  CHECK(!synthetic(base + "\ninert=['-O2']").ok());
}
TEST(profile_ordered_macro_operations_and_exact_replacements) {
  auto text = replace(allowProfile(), "flags = [\"-DONLINE_JUDGE\"]", "flags = ['-DX=1','-UX','-U','Y','-D','Y=0','-DEMPTY=','-DONE','-DF(x)=x+1']");
  auto r = synthetic(text); CHECK(r.ok()); if (!r.ok()) return;
  const auto& p = r.value(); CHECK_EQ(p.macroOperations.size(), 7u);
  CHECK_EQ(p.initialState("X").state, MacroState::undefined);
  CHECK_EQ(p.initialState("Y").definition->replacement, "0");
  CHECK_EQ(p.initialState("EMPTY").definition->replacement, "");
  CHECK_EQ(p.initialState("ONE").definition->replacement, "1");
  CHECK(p.initialState("F").definition->functionLike);
  CHECK(p.checkA3({"X"}).ok()); CHECK(!p.checkA3({"Y", "EMPTY"}).ok());
  for (const auto* f : {"-D2BAD", "-Ubad(x)", "-D"}) CHECK(!synthetic(replace(allowProfile(), "-DONLINE_JUDGE", f)).ok());
}
TEST(profile_special_builtin_state_follows_probe_and_command_operations) {
  auto text = test::read(profileFixture("mingw.toml")); auto snapshot = test::read(profileFixture("mingw-predefined.txt"));
  auto p = synthetic(text).value();
  CHECK(p.initialState("__has_include").activeSpecialBuiltin());
  CHECK_EQ(p.initialState("__FILE_NAME__").state, MacroState::definedUnknown);
  auto no = replace(snapshot, "# state __has_include defined-unknown special-builtin", "# state __has_include undefined command-operation");
  no = replace(no, "# probe __has_include defined", "# probe __has_include undefined");
  no = replace(no, "# command-operation #define ONLINE_JUDGE 1", "");
  no = replace(no, "# flag: -DONLINE_JUDGE", "# flag: -U__has_include");
  auto u = synthetic(replace(text, "-DONLINE_JUDGE", "-U__has_include"), no);
  CHECK(u.ok()); if (u.ok()) CHECK_EQ(u.value().initialState("__has_include").state, MacroState::undefined);
  auto yes = replace(snapshot, "# state __has_include defined-unknown special-builtin", "# command-operation #define __has_include(x) 0");
  yes = replace(yes, "# command-operation #define ONLINE_JUDGE 1", "");
  yes = replace(yes, "# flag: -DONLINE_JUDGE", "# flag: -D__has_include(x)=0");
  auto d = synthetic(replace(text, "-DONLINE_JUDGE", "-D__has_include(x)=0"), yes);
  CHECK(d.ok()); if (d.ok()) { CHECK(!d.value().initialState("__has_include").activeSpecialBuiltin()); CHECK_EQ(d.value().initialState("__has_include").definition->replacement, "0"); }
}
TEST(profile_snapshot_format_metadata_provenance_and_validation) {
  const auto text = test::read(profileFixture("mingw.toml")), snapshot = test::read(profileFixture("mingw-predefined.txt"));
  for (auto [before, after] : {std::pair{"# distill-snapshot: 1", "# distill-snapshot: 2"},
       {"# reproduced: yes", "# reproduced: maybe"}, {"# gcc-version: 14.2.0", "# gcc-version: 15.2.0"},
       {"# builtin-registry: gcc-14.2", "# builtin-registry: none"}, {"# native-model: none", "# native-model: gcc-14.2-x86"},
       {"# flag: -DONLINE_JUDGE", "# flag: -UOTHER"}, {"# end-header", ""},
       {"# probe __has_include defined", ""}, {"# probe __has_include defined", "# probe __has_include undefined"},
       {"#define __GNUC__ 14", "#define __GNUC__ 15"}}) CHECK(!synthetic(text, replace(snapshot, before, after)).ok());
  for (const auto* extra : {"#undef BAD", "# typo whatever", "garbage", "#define __GNUC__ 14", "# state ABC unknown strange", "# state __mystery unknown native-model", "# probe __new_builtin defined"}) CHECK(!synthetic(text, snapshot + extra + "\n").ok());
  auto startup = synthetic(text, snapshot + "# startup-header stdc-predef.h #define __STARTUP_TEST 1\n");
  CHECK(startup.ok()); if (startup.ok()) CHECK_EQ(startup.value().initialState("__STARTUP_TEST").provenance.front(), FactOrigin::startupHeader);
  CHECK(compatibleProfileTriple("x86_64-pc-linux-gnu", "x86_64-linux-gnu"));
  CHECK(compatibleProfileTriple("x86_64-w64-mingw32", "x86_64-pc-mingw32"));
  CHECK(!compatibleProfileTriple("x86_64-linux-gnu", "aarch64-linux-gnu"));
  CHECK(!compatibleProfileTriple("x86_64-linux-gnu", "x86_64-linux-musl"));
}
TEST(profile_snapshot_missing_never_downgrades_and_a3_reports_collisions) {
  auto text = test::read(profileFixture("mingw.toml"));
  auto r = parseProfile(text, "mingw.toml"); CHECK(!r.ok()); CHECK(r.error().message.find("snapshot missing") != std::string::npos);
  for (const auto* name : {"codeforces", "atcoder"}) { auto builtin = lookupProfile(name); CHECK(!builtin.ok()); if (!builtin.ok()) CHECK(builtin.error().message.find("snapshot missing") != std::string::npos); }
  auto p = synthetic(text).value(); CHECK(p.checkA3({"LOCAL", "GUARD"}).ok());
  auto conflict = p.checkA3({"ONLINE_JUDGE", "WIN32"}); CHECK(!conflict.ok());
  CHECK(conflict.error().message.find("ONLINE_JUDGE") != std::string::npos); CHECK(conflict.error().message.find("WIN32") != std::string::npos);
}
TEST(profile_lookup_user_directory_environment_embedded_and_path_forms) {
  test::TempDirectory temp;
  auto text = allowProfile();
  ProfileFiles files{{"mingw.toml", replace(text, "Synthetic MinGW fixture", "Embedded fixture")}};
  ProfileLookup lookup; lookup.userDirectory = temp.path;
  auto builtin = lookupProfile("mingw", lookup, files); CHECK(builtin.ok()); CHECK_EQ(builtin.value().description, "Embedded fixture");
  temp.write("mingw.toml", text);
  auto user = lookupProfile("mingw", lookup, files); CHECK(user.ok()); CHECK_EQ(user.value().description, "Synthetic MinGW fixture");
  auto path = lookupProfile(pathToUtf8(temp.path / "mingw.toml"), {}, files); CHECK(path.ok()); CHECK_EQ(path.value().provenance.file, temp.path / "mingw.toml");
  temp.write("mingw.toml", text + "\ntypo = true"); CHECK(!lookupProfile("mingw", lookup, files).ok());
  CHECK(!lookupProfile("absent", lookup, files).ok()); CHECK(!lookupProfile("BAD", lookup, files).ok());
  CHECK(!lookupProfile("mingw.toml", lookup, files).ok()); // Path form does not search by name.
  auto snapshotFiles = ProfileFiles{{"mingw.toml", test::read(profileFixture("mingw.toml"))}, {"mingw-predefined.txt", test::read(profileFixture("mingw-predefined.txt"))}};
  auto embedded = lookupProfile("mingw", {}, snapshotFiles); CHECK(embedded.ok()); CHECK(embedded.value().reproduced);
}
TEST(profile_lookup_environment_is_injected_for_posix_and_windows) {
  ProfileLookup lookup; lookup.windows = false;
  CHECK(!profileUserDirectory(lookup));
  lookup.environment = {{"HOME", "/test-home"}, {"XDG_CONFIG_HOME", "/test-xdg"}};
#ifndef _WIN32
  CHECK_EQ(pathToUtf8(*profileUserDirectory(lookup)), "/test-xdg/distill/judges");
  lookup.environment["XDG_CONFIG_HOME"] = "relative";
  CHECK_EQ(pathToUtf8(*profileUserDirectory(lookup)), "/test-home/.config/distill/judges");
#endif
  lookup.environment["XDG_CONFIG_HOME"] = "";
  lookup.environment.clear(); lookup.windows = true;
  CHECK(!profileUserDirectory(lookup));
  lookup.environment["APPDATA"] = "app-data";
  CHECK_EQ(*profileUserDirectory(lookup), pathFromUtf8("app-data") / "distill" / "judges");
  lookup.userDirectory = "explicit"; CHECK_EQ(*profileUserDirectory(lookup), std::filesystem::path("explicit"));
  test::TempDirectory temp;
  auto config = temp.path / "distill" / "judges"; std::filesystem::create_directories(config);
  std::ofstream(config / "mingw.toml") << allowProfile();
  lookup.userDirectory.reset(); lookup.environment["APPDATA"] = pathToUtf8(temp.path);
  CHECK(lookupProfile("mingw", lookup, {}).ok());
  lookup.windows = false; lookup.environment["XDG_CONFIG_HOME"] = pathToUtf8(temp.path);
  CHECK(lookupProfile("mingw", lookup, {}).ok());
}

TEST(profile_file_errors_and_relative_snapshot_lookup) {
  test::TempDirectory temp;
  CHECK(!loadProfile(temp.path / "missing.toml").ok());
  CHECK(!loadProfile(temp.path).ok());
  temp.write("mingw.toml", test::read(profileFixture("mingw.toml")));
  auto missing = loadProfile(temp.path / "mingw.toml"); CHECK(!missing.ok());
  if (!missing.ok()) CHECK(missing.error().message.find("snapshot missing") != std::string::npos);
  temp.write("mingw-predefined.txt", test::read(profileFixture("mingw-predefined.txt")));
  CHECK(loadProfile(temp.path / "mingw.toml").ok());
  temp.write("mingw-predefined.txt", "# metadata\nnot a macro\n");
  auto malformed = loadProfile(temp.path / "mingw.toml"); CHECK(!malformed.ok());
  if (!malformed.ok()) CHECK(malformed.error().message.find("snapshot line 1") != std::string::npos);
}
TEST(profile_toml_schema_accepts_escaped_description_and_quoted_schema_keys) {
  auto text = replace(allowProfile(), "description = \"Synthetic MinGW fixture\"", "'description' = \"Line 1\\nLine 2\\t\\u03B1\"");
  auto result = synthetic(text); CHECK(result.ok());
  if (result.ok()) CHECK_EQ(result.value().description, "Line 1\nLine 2\tα");
}

TEST(profile_native_model_states_and_missing_records) {
  auto text = replace(test::read(profileFixture("mingw.toml")), "flags = [\"-DONLINE_JUDGE\"]", "flags = [\"-DONLINE_JUDGE\", \"-mtune=native\"]");
  auto snapshot = test::read(profileFixture("mingw-predefined.txt"));
  snapshot = replace(snapshot, "# flag: -DONLINE_JUDGE", "# flag: -DONLINE_JUDGE\n# flag: -mtune=native");
  snapshot = replace(snapshot, "# native-model: none", "# native-model: gcc-14.2-x86");
  for (const auto& [name, scope] : nativeDependencies({CompilerFamily::gcc, 14, 2, 0})) if (scope == "tune") snapshot += "# state " + name + " undefined native-model\n";
  snapshot = replace(snapshot, "# state __GCC_DESTRUCTIVE_SIZE undefined native-model", "# state __GCC_DESTRUCTIVE_SIZE defined-unknown native-model");
  auto r = synthetic(text, snapshot); CHECK(r.ok()); if (!r.ok()) return;
  CHECK_EQ(r.value().initialState("__GCC_DESTRUCTIVE_SIZE").state, MacroState::definedUnknown);
  CHECK_EQ(r.value().initialState("__GCC_DESTRUCTIVE_SIZE").provenance.front(), FactOrigin::nativeModel);
  CHECK_EQ(r.value().initialState("__x86_64__").state, MacroState::known);
  CHECK(!synthetic(text, replace(snapshot, "# state __GCC_CONSTRUCTIVE_SIZE undefined native-model", "")).ok());
  CHECK(!synthetic(text, snapshot + "# state __NOT_IN_MODEL unknown native-model\n").ok());
}
TEST(profile_command_snapshot_must_agree_and_only_link_flags_are_accepted) {
  auto text = test::read(profileFixture("mingw.toml")), snapshot = test::read(profileFixture("mingw-predefined.txt"));
  CHECK(!synthetic(text, replace(snapshot, "# command-operation #define ONLINE_JUDGE 1", "# command-operation #define ONLINE_JUDGE 2")).ok());
  CHECK(!synthetic(text, replace(snapshot, "# command-operation #define ONLINE_JUDGE 1", "#define ONLINE_JUDGE 1")).ok());
  CHECK(!synthetic(text, snapshot + "# command-operation #define UNDECLARED 1\n").ok());
  CHECK(synthetic(text + "\nlink=['-static','-Wl,--stack=268435456','-lstdc++exp']").ok());
  CHECK(!synthetic(text + "\nlink=['-DBAD=1']").ok());
  CHECK(!synthetic(text + "\nlink=['-O2']").ok());
}
