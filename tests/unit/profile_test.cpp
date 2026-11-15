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
  return replace(test::read(profileFixture("mingw.toml")), "predefined = \"mingw-predefined.txt\"", "allowed = [\"__GNUC__=14\", \"_WIN64=1\", \"FACT=42\"]");
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
TEST(profile_loads_local_linux_snapshot_and_provenance) {
  auto r = loadProfile(profileFixture("linux.toml")); CHECK(r.ok()); if (!r.ok()) return;
  const auto& p = r.value();
  CHECK(p.exhaustive); CHECK_EQ(p.triple, "aarch64-linux-gnu");
  CHECK_EQ(p.compiler.family, CompilerFamily::gcc); CHECK_EQ(p.compiler.major, 15); CHECK_EQ(p.compiler.minor, 2); CHECK_EQ(p.compiler.patch, 0); CHECK(p.patchSpecified);
  CHECK_EQ(p.dialect.name, "gnu++23"); CHECK(p.dialect.gnu); CHECK_EQ(p.dialect.cplusplus, 202302);
  CHECK(p.predefined.size() > 400); CHECK(p.predefined.contains("__aarch64__"));
  CHECK_EQ(p.judgeMacros.at("ONLINE_JUDGE").replacement, "1"); CHECK_EQ(p.judgeMacros.at("EMPTY").replacement, ""); CHECK_EQ(p.judgeMacros.at("VALUE").replacement, "42");
  CHECK(!p.predefined.contains("ONLINE_JUDGE"));
  CHECK_EQ(p.library, "libstdc++"); CHECK_EQ(p.check, (std::vector<std::string>{"g++"}));
  CHECK_EQ(p.provenance.file, profileFixture("linux.toml")); CHECK_EQ(p.provenance.updated, "2026-11-15"); CHECK_EQ(p.provenance.sources.size(), 1u);
}
TEST(profile_loads_synthetic_mingw_and_allow_list_only) {
  auto r = loadProfile(profileFixture("mingw.toml")); CHECK(r.ok()); if (!r.ok()) return;
  CHECK(r.value().exhaustive); CHECK(!r.value().patchSpecified); CHECK(!r.value().dialect.gnu);
  CHECK(r.value().predefined.at("FUNCTION").functionLike); CHECK(r.value().predefined.at("FUNCTION").variadic);
  auto a = synthetic(allowProfile()); CHECK(a.ok()); CHECK(!a.value().exhaustive); CHECK(!a.value().snapshot); CHECK_EQ(a.value().predefined.size(), 3u);
  auto empty = synthetic(replace(allowProfile(), "allowed = [\"__GNUC__=14\", \"_WIN64=1\", \"FACT=42\"]", ""));
  CHECK(empty.ok()); CHECK(!empty.value().exhaustive); CHECK(empty.value().predefined.empty());
}
TEST(profile_schema_required_keys_unknown_keys_and_wrong_types) {
  auto text = allowProfile(); auto doc = parseProfileToml(text, "test").value();
  for (const auto& [key, entry] : doc) {
    if (key == "allowed") continue;
    auto begin = text.find(key + " ="); auto end = text.find('\n', begin);
    auto missing = text; missing.erase(begin, end - begin); badProfile(missing, "required");
    auto wrong = text; wrong.replace(begin + key.size() + 3, end - (begin + key.size() + 3), "true"); badProfile(wrong, "wrong type");
    (void)entry;
  }
  badProfile(text + "\nunknown=1\n", "unknown key");
  badProfile(text + "\n'compiler.family'='gcc'\n", "unknown key");
  badProfile(replace(text, "allowed = [\"__GNUC__=14\", \"_WIN64=1\", \"FACT=42\"]", "allowed = 'FACT'"), "wrong type");
  badProfile(replace(text, "allowed = [\"__GNUC__=14\", \"_WIN64=1\", \"FACT=42\"]", "predefined = false"), "wrong type");
}
TEST(profile_schema_validates_names_version_triple_std_defines_and_library) {
  const auto text = allowProfile();
  for (auto [before, after, error] : {std::tuple{"name = \"mingw\"", "name = \"Bad_Name\"", "name:"}, {"name = \"mingw\"", "name = \"other\"", "name:"}, {"triple = \"x86_64-w64-mingw32\"", "triple = \"bad\"", "triple:"}, {"family = \"gcc\"", "family = \"clang\"", "only gcc"}, {"version = \"14.2\"", "version = \"14\"", "major.minor"}, {"version = \"14.2\"", "version = \"14.2.0.1\"", "major.minor"}, {"version = \"14.2\"", "version = \"999999999999999.2\"", "version"}, {"version = \"14.2\"", "version = \"0.2\"", "version"}, {"family = \"gcc\", version = \"14.2\"", "family = \"gcc\"", "requires only"}, {"family = \"gcc\", version = \"14.2\"", "family = \"gcc\", version = \"14.2\", typo = \"x\"", "requires only"}, {"std = \"c++23\"", "std = \"-std=c++23\"", "c++NN"}, {"std = \"c++23\"", "std = \"gnu++2b\"", "c++NN"}, {"std = \"c++23\"", "std = \"c++99\"", "unsupported"}, {"defines = [\"ONLINE_JUDGE\"]", "defines = [\"2BAD\"]", "NAME"}, {"defines = [\"ONLINE_JUDGE\"]", "defines = [\"BAD(x)=1\"]", "NAME"}, {"defines = [\"ONLINE_JUDGE\"]", "defines = [\"X\", \"X=2\"]", "malformed macro"}, {"library = \"mingw-w64\"", "library = \"unknown\"", "library:"}, {"check = [\"x86_64-w64-mingw32-g++\"]", "check = ['']", "compiler name"}}) badProfile(replace(text, before, after), error);
  auto line = synthetic(text + "\ntypo = 'bad'\n"); CHECK(!line.ok());
  CHECK(line.error().message.find(": unknown key") != std::string::npos);
}
TEST(profile_schema_validates_dates_sources_and_snapshot_filename) {
  const auto text = allowProfile();
  for (const auto* date : {"2026-02-29", "2026-04-31", "2026-13-01", "2026-00-01", "2026-01-00", "0000-01-01", "2026-1-1", "today"}) badProfile(replace(text, "2026-11-15", date), "updated:");
  CHECK(synthetic(replace(text, "2026-11-15", "2024-02-29")).ok());
  for (const auto* sources : {"[]", "['']", "['not-url']", "['file:///test']", "['https://']", "['https://?']", "['https://#']", "['https://bad host/']"}) badProfile(replace(text, "[\"https://gcc.gnu.org/gcc-14/\"]", sources), "sources:");
  auto snapshotText = test::read(profileFixture("mingw.toml"));
  for (const auto* file : {"../escape.txt", "/absolute.txt", "C:drive.txt", "sub\\\\file.txt", ".", ".."}) badProfile(replace(snapshotText, "mingw-predefined.txt", file), "relative to the profile");
  badProfile(snapshotText + "\nallowed = []\n", "only valid without");
}
TEST(profile_snapshot_missing_is_clear_and_never_downgrades) {
  const auto text = test::read(profileFixture("mingw.toml"));
  auto r = parseProfile(text, "mingw.toml"); CHECK(!r.ok()); CHECK(r.error().message.find("snapshot missing") != std::string::npos);
  auto other = parseProfile(text, "mingw.toml", [](const auto&) -> Result<std::string> { return Error{"fixture read failed"}; });
  CHECK(!other.ok()); CHECK(other.error().message.find("snapshot missing") != std::string::npos);
  for (const auto* name : {"codeforces", "atcoder"}) {
    auto builtin = lookupProfile(name); CHECK(!builtin.ok());
    if (!builtin.ok()) CHECK(builtin.error().message.find("snapshot missing") != std::string::npos);
  }
}
TEST(profile_snapshot_validates_identity_platform_arch_dialect_and_syntax) {
  const auto text = test::read(profileFixture("mingw.toml"));
  const auto snapshot = test::read(profileFixture("mingw-predefined.txt"));
  for (auto [before, after] : {std::pair{"#define __GNUC__ 14", "#define __GNUC__ 15"}, {"#define __GNUC_MINOR__ 2", "#define __GNUC_MINOR__ 3"}, {"#define __cplusplus 202302L", "#define __cplusplus 202002L"}, {"#define __x86_64__ 1", ""}, {"#define _WIN32 1", ""}, {"#define _WIN64 1", ""}, {"#define __STRICT_ANSI__ 1", ""}, {"#define __GNUC__ 14", "#define __GNUC__(x) 14"}, {"#define __GNUC__ 14", "#define __GNUC__ 14junk"}, {"#define __GNUC__ 14", "#define __GNUC__ 99999999999999999999999999"}, {"#define __GNUC_MINOR__ 2", "#define __GNUC_MINOR__ 2\n#define __GNUC_MINOR__ 2"}}) {
    auto r = synthetic(text, replace(snapshot, before, after)); CHECK(!r.ok());
  }
  for (const auto* extra : {"#define __clang__ 1", "#define __linux__ 1", "#define __aarch64__ 1", "#define __i386__ 1", "#undef BAD", "garbage", "#define BAD \xff"}) CHECK(!synthetic(text, snapshot + extra + "\n").ok());
  auto patch = replace(text, "version = \"14.2\"", "version = \"14.2.1\""); CHECK(!synthetic(patch).ok());
  auto gnu = replace(text, "std = \"c++23\"", "std = \"gnu++23\""); CHECK(!synthetic(gnu).ok());
  CHECK(synthetic(gnu, replace(snapshot, "#define __STRICT_ANSI__ 1", "")).ok());
  auto linux = test::read(profileFixture("linux.toml"));
  auto linuxSnapshot = test::read(profileFixture("linux-predefined.txt"));
  CHECK(!parseProfile(linux, "linux.toml", [&](const auto&) -> Result<std::string> { return replace(linuxSnapshot, "#define __linux__ 1", ""); }).ok());
}
TEST(profile_allow_list_consistency_and_a3) {
  auto text = allowProfile();
  badProfile(replace(text, "__GNUC__=14", "__GNUC__=13"), "disagrees");
  badProfile(replace(text, "_WIN64=1", "__linux__=1"), "disagrees");
  auto p = synthetic(text).value();
  CHECK(p.checkA3({"GUARD", "LOCAL"}).ok());
  auto r = p.checkA3({"ONLINE_JUDGE", "FACT", "__GNUC__"}); CHECK(!r.ok());
  CHECK(r.error().message.find("A3 conflict") != std::string::npos);
  for (const auto* name : {"ONLINE_JUDGE", "FACT", "__GNUC__"}) CHECK(r.error().message.find(name) != std::string::npos);
  auto exhaustive = loadProfile(profileFixture("mingw.toml")).value();
  CHECK(!exhaustive.checkA3({"WIN32"}).ok()); CHECK(exhaustive.checkA3({"FACT"}).ok());
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
  auto embedded = lookupProfile("mingw", {}, snapshotFiles); CHECK(embedded.ok()); CHECK(embedded.value().exhaustive);
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
  if (!malformed.ok()) CHECK(malformed.error().message.find("line 2") != std::string::npos);
}
TEST(profile_toml_schema_accepts_escaped_description_and_quoted_schema_keys) {
  auto text = replace(allowProfile(), "description = \"Synthetic MinGW fixture\"", "'description' = \"Line 1\\nLine 2\\t\\u03B1\"");
  auto result = synthetic(text); CHECK(result.ok());
  if (result.ok()) CHECK_EQ(result.value().description, "Line 1\nLine 2\tα");
}
