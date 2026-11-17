// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.h"
#include "core/profile/profile.h"
#include "core/profile/toml.h"
#include "core/profile/sha256.h"
#include "core/report/report.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <iomanip>
#include <sstream>
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
std::string quotedPath(const std::filesystem::path& path) {
  std::ostringstream out; out << std::quoted(pathToUtf8(path)); return out.str();
}
std::string allowProfile() {
  auto text = test::read(profileFixture("mingw-gcc14-cpp23.toml"));
  text = replace(text, "predefined = \"mingw-predefined.txt\"", "");
  return text;
}
Result<Profile> synthetic(std::string text, std::string snapshot = test::read(profileFixture("mingw-predefined.txt"))) {
  return parseProfile(text, "mingw-gcc14-cpp23.toml", [snapshot](const auto&) -> Result<std::string> { return snapshot; });
}
void badProfile(const std::string& text, std::string_view reason) {
  auto result = synthetic(text);
  CHECK(!result.ok());
  if (!result.ok()) {
    CHECK(result.error().message.starts_with("mingw-gcc14-cpp23.toml:"));
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
  auto r = loadProfile(profileFixture("mingw-gcc14-cpp23.toml")); CHECK(r.ok()); if (!r.ok()) return;
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
  auto linux = loadProfile(profileFixture("linux-gcc15-gnupp23.toml")); CHECK(linux.ok()); if (!linux.ok()) return;
  CHECK(!linux.value().reproduced); CHECK(linux.value().predefined.size() > 400);
  CHECK_EQ(linux.value().initialState("__aarch64__").state, MacroState::known);
  CHECK_EQ(linux.value().initialState("__aarch64__").provenance.back(), FactOrigin::allowList);
  CHECK_EQ(linux.value().initialState("__OPTIMIZE__").state, MacroState::unknown);
  CHECK_EQ(linux.value().initialState("__CHAR_UNSIGNED__").state, MacroState::known);
  CHECK_EQ(linux.value().initialState("__absent_reserved").state, MacroState::unknown);
  CHECK_EQ(linux.value().initialState("LOCAL").state, MacroState::undefined);
  CHECK_EQ(linux.value().initialState("EMPTY").definition->replacement, "");
}
TEST(profile_embedded_shipped_codeforces_snapshot) {
  const auto loaded = lookupProfile("codeforces");
  if (!loaded.ok()) throw std::runtime_error(loaded.error().message);
  const auto& p = loaded.value();
  CHECK_EQ(p.provenance.file, pathFromUtf8("embedded/codeforces-gcc14-cpp23.toml"));
  CHECK(!p.reproduced);
  for (const auto* name : {"_WIN32", "_WIN64", "__MINGW64__", "__x86_64__", "__STRICT_ANSI__"})
    CHECK_EQ(p.initialState(name).state, MacroState::known);
  const auto linux = p.initialState("__linux__");
  CHECK_EQ(linux.state, MacroState::undefined);
  CHECK_EQ(linux.provenance, (std::vector{FactOrigin::allowList}));
  for (const auto& [name, value] : {std::pair{"__SIZEOF_LONG__", "4"}, {"ONLINE_JUDGE", "1"}}) {
    const auto fact = p.initialState(name);
    CHECK_EQ(fact.state, MacroState::known);
    CHECK(fact.definition.has_value());
    if (fact.definition) CHECK_EQ(fact.definition->replacement, value);
  }
  CHECK_EQ(p.initialState("ONLINE_JUDGE").provenance, (std::vector{FactOrigin::commandOperation}));
  const auto local = p.initialState("LOCAL");
  CHECK_EQ(local.state, MacroState::undefined);
  CHECK_EQ(local.provenance, (std::vector{FactOrigin::a5}));
  CHECK(!approximateAllowed("__SOME_VENDOR_MACRO__"));
  CHECK(!p.snapshotFacts.contains("__SOME_VENDOR_MACRO__"));
  CHECK_EQ(p.initialState("__SOME_VENDOR_MACRO__").state, MacroState::unknown);
  const auto hasInclude = p.initialState("__has_include");
  CHECK_EQ(hasInclude.state, MacroState::definedUnknown);
  CHECK(!hasInclude.definition);
  CHECK(hasInclude.activeSpecialBuiltin());
}
TEST(profile_embedded_shipped_atcoder_snapshot) {
  const auto loaded = lookupProfile("atcoder");
  if (!loaded.ok()) throw std::runtime_error(loaded.error().message);
  const auto& p = loaded.value();
  CHECK_EQ(p.provenance.file, pathFromUtf8("embedded/atcoder-gcc15-gnupp23.toml"));
  CHECK(p.reproduced);
  CHECK_EQ(p.snapshotMetadata.dumpmachine, "x86_64-pc-linux-gnu");
  for (const auto* name : {"__linux__", "_OPENMP", "_REENTRANT", "__OPTIMIZE__", "__cpp_modules"})
    CHECK_EQ(p.initialState(name).state, MacroState::known);
  // Every native-model spelling is unknown under -march=native (design §4.3),
  // including ABI-fixed ones; an audited ABI scope is a planned refinement.
  for (const auto* name : {"__x86_64__", "__SSE2__", "__SHSTK__", "__GCC_DESTRUCTIVE_SIZE"}) {
    const auto fact = p.initialState(name);
    CHECK_EQ(fact.state, MacroState::unknown);
    CHECK_EQ(fact.provenance, (std::vector{FactOrigin::nativeModel}));
  }
  const auto win32 = p.initialState("_WIN32");
  CHECK_EQ(win32.state, MacroState::undefined);
  CHECK_EQ(win32.provenance, (std::vector{FactOrigin::a4}));
  const auto avx2 = p.initialState("__AVX2__");
  CHECK_EQ(avx2.state, MacroState::unknown);
  CHECK_EQ(avx2.provenance, (std::vector{FactOrigin::nativeModel}));
  for (const auto& [name, value] : {std::pair{"ATCODER", "1"}, {"OR_PROTO_DLL", ""}}) {
    const auto fact = p.initialState(name);
    CHECK_EQ(fact.state, MacroState::known);
    CHECK(fact.definition.has_value());
    if (fact.definition) CHECK_EQ(fact.definition->replacement, value);
    CHECK_EQ(fact.provenance, (std::vector{FactOrigin::commandOperation}));
  }
  const auto local = p.initialState("LOCAL");
  CHECK_EQ(local.state, MacroState::undefined);
  CHECK_EQ(local.provenance, (std::vector{FactOrigin::a5}));
  CHECK_EQ(p.initialState("__STRICT_ANSI__").state, MacroState::undefined);
}
TEST(profile_mode_requires_matching_build_and_complete_registry) {
  auto text = test::read(profileFixture("mingw-gcc14-cpp23.toml"));
  auto snapshot = test::read(profileFixture("mingw-predefined.txt"));
  for (const auto& other : {replace(text, "build = { package = \"synthetic:fixture=14.2.0-1\" }", ""),
                           replace(text, "synthetic:fixture=14.2.0-1", "other:build=1")}) {
    const auto inventory = other.find("build =") == std::string::npos ? replace(snapshot, "# reproduced: yes", "# reproduced: no") : snapshot;
    auto r = synthetic(other, inventory); CHECK(r.ok()); if (r.ok()) {
      CHECK(!r.value().reproduced);
      CHECK_EQ(r.value().initialState("__has_include").state, MacroState::definedUnknown);
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
  const auto original = test::read(profileFixture("mingw-gcc14-cpp23.toml"));
  const auto text = replace(original, "build = { package = \"synthetic:fixture=14.2.0-1\" }",
                            "snapshot_compiler = { package = \"msys2:mingw-w64-x86_64-gcc=14.2.0-3\" }");
  const auto snapshot = replace(test::read(profileFixture("mingw-predefined.txt")),
      "# build: package=synthetic:fixture=14.2.0-1", "# build: none\n# snapshot-compiler: package=msys2:mingw-w64-x86_64-gcc=14.2.0-3");
  auto r = synthetic(text, replace(snapshot, "# reproduced: yes", "# reproduced: no"));
  CHECK(r.ok()); if (!r.ok()) return;
  CHECK(!r.value().build); CHECK(r.value().snapshotCompiler.has_value()); CHECK(!r.value().reproduced);
  CHECK_EQ(r.value().snapshotMetadata.snapshotCompiler, "package=msys2:mingw-w64-x86_64-gcc=14.2.0-3");
  CHECK_EQ(r.value().initialState("__GNUC__").state, MacroState::known);
  CHECK_EQ(r.value().initialState("__has_include").state, MacroState::definedUnknown);
  CHECK_EQ(r.value().initialState("__absent_reserved").state, MacroState::unknown);
  auto claimed = synthetic(text, snapshot); CHECK(!claimed.ok());
  if (!claimed.ok()) CHECK(claimed.error().message.find("requires build provenance") != std::string::npos);
  CHECK(!synthetic(replace(original, "build =", "snapshot_compiler =")).ok()); // header must record it
  CHECK(!synthetic(text, replace(snapshot, "gcc=14.2.0-3", "gcc=14.2.0-2")).ok());
  for (const auto* value : {"{}", "{package=''}", "{package='gcc'}", "{package='gcc=1 2'}", "{recipe='not-url'}", "{typo='x'}", "{package='gcc=1',recipe='https://x'}", "[]"})
    badProfile(allowProfile() + "\nsnapshot_compiler = " + value, "snapshot_compiler:");
}
TEST(profile_gcc13_cpp23_draft_cplusplus_value) {
  auto text = test::read(profileFixture("mingw-gcc14-cpp23.toml"));
  text = replace(text, "mingw-gcc14-cpp23", "mingw-gcc13-cpp23");
  text = replace(text, "version = \"14.2\"", "version = \"13.3.0\"");
  text = replace(text, "build = { package = \"synthetic:fixture=14.2.0-1\" }", "");
  // Synthetic protocol fixture only; never a captured or shipped CSES snapshot.
  const std::string snapshot = R"snapshot(# distill-snapshot: 1
# compiler-path: /synthetic/g++
# compiler-version: synthetic GCC 13.3.0
# gcc-version: 13.3.0
# dumpmachine: x86_64-w64-mingw32
# profile-triple: x86_64-w64-mingw32
# build: none
# reproduced: no
# std: c++23
# builtin-registry: none
# native-model: none
# flag: -DONLINE_JUDGE
# end-header
#define __GNUC__ 13
#define __GNUC_MINOR__ 3
#define __GNUC_PATCHLEVEL__ 0
#define __cplusplus 202100L
#define __STRICT_ANSI__ 1
#define __x86_64__ 1
#define _WIN32 1
#define _WIN64 1
# command-operation #define ONLINE_JUDGE 1
)snapshot";
  auto load = [&](const std::string& contents) {
    return parseProfile(text, "mingw-gcc13-cpp23.toml", [&](const auto&) -> Result<std::string> { return contents; });
  };
  const auto valid = load(snapshot);
  CHECK(valid.ok());
  if (valid.ok()) {
    CHECK(!valid.value().reproduced);
    CHECK_EQ(valid.value().initialState("__cplusplus").definition->replacement, "202100L");
  }
  CHECK(!load(replace(snapshot, "202100L", "202002L")).ok());
  CHECK(!load(replace(snapshot, "202100L", "202302L")).ok());
}
TEST(profile_ubuntu_package_provenance_requires_release_and_revision) {
  for (const auto* key : {"build", "snapshot_compiler"}) {
    const auto base = replace(allowProfile(), "build = { package = \"synthetic:fixture=14.2.0-1\" }", "");
    const auto text = base + "\n" + key + " = { package = 'ubuntu-24.04:g++-13=13.3.0-6ubuntu2~24.04.1' }";
    auto valid = synthetic(text);
    CHECK(valid.ok());
    if (valid.ok()) {
      CHECK(!valid.value().reproduced); // package identity alone never affirms reproduction
      const auto& identity = std::string_view(key) == "build" ? valid.value().build : valid.value().snapshotCompiler;
      CHECK(identity.has_value());
      if (identity) CHECK_EQ(identity->second, "ubuntu-24.04:g++-13=13.3.0-6ubuntu2~24.04.1");
    }
    for (const auto* package : {"ubuntu:g++-13=13.3.0-1", "ubuntu-24:g++-13=13.3.0-1",
                                "ubuntu-24.04:=13.3.0-1", "ubuntu-24.04:g++-13=13.3.0",
                                "ubuntu-24.04:g++-13=13.3.0-", "ubuntu-24.04:g++-13=13.3.0-*"})
      badProfile(base + "\n" + key + " = { package = '" + package + "' }", std::string(key) + ":");
  }
}
TEST(profile_image_provenance_is_digest_pinned_and_snapshot_only) {
  const std::string image = "docker.io/synthetic/compiler@sha256:" + std::string(64, 'a');
  const auto base = replace(allowProfile(), "build = { package = \"synthetic:fixture=14.2.0-1\" }", "");
  const auto declaration = "\nsnapshot_compiler = { image = '" + image + "' }";
  auto valid = synthetic(base + declaration);
  CHECK(valid.ok()); if (!valid.ok()) return;
  CHECK(!valid.value().build); CHECK(!valid.value().reproduced);
  CHECK(valid.value().snapshotCompiler.has_value());
  if (valid.value().snapshotCompiler) CHECK_EQ(*valid.value().snapshotCompiler, (std::pair{std::string("image"), image}));
  badProfile(base + "\nbuild = { image = '" + image + "' }", "build:");
  for (const auto& value : {"synthetic/compiler@sha256:" + std::string(64, 'a'),
                           std::string("docker.io/synthetic/compiler"), std::string("docker.io/synthetic/compiler:latest"),
                           replace(image, "@", ":latest@"), image.substr(0, image.size() - 1), image + "a",
                           replace(image, "sha256:", "sha512:"), replace(image, std::string(64, 'a'), std::string(64, 'A')),
                           replace(image, "/synthetic/", "/Synthetic/"), replace(image, "/synthetic/", "//"),
                           replace(image, "/synthetic/", "/../"), image + " "})
    badProfile(base + "\nsnapshot_compiler = { image = '" + value + "' }", "snapshot_compiler:");
  badProfile(base + "\nsnapshot_compiler = { image = '" + image + "', package = 'gcc=14.2.0' }", "snapshot_compiler:");

  // Synthetic protocol fixture only; image provenance never affirms reproduction.
  const auto original = test::read(profileFixture("mingw-gcc14-cpp23.toml"));
  const auto text = replace(original, "build = { package = \"synthetic:fixture=14.2.0-1\" }", "") + declaration;
  auto snapshot = replace(test::read(profileFixture("mingw-predefined.txt")),
      "# build: package=synthetic:fixture=14.2.0-1", "# build: none\n# snapshot-compiler: image=" + image);
  snapshot = replace(snapshot, "# reproduced: yes", "# reproduced: no");
  const auto loaded = synthetic(text, snapshot);
  CHECK(loaded.ok()); if (loaded.ok()) {
    CHECK(!loaded.value().reproduced);
    CHECK_EQ(loaded.value().snapshotMetadata.snapshotCompiler, "image=" + image);
  }
  CHECK(!synthetic(text, replace(snapshot, "# reproduced: no", "# reproduced: yes")).ok());
  CHECK(!synthetic(text, replace(snapshot, image, image.substr(0, image.size() - 1) + "b")).ok());
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
  for (auto [before, after, error] : {std::tuple{"name = \"mingw-gcc14-cpp23\"", "name = \"Bad_Name\"", "name:"},
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
  auto text = test::read(profileFixture("mingw-gcc14-cpp23.toml")); auto snapshot = test::read(profileFixture("mingw-predefined.txt"));
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
  const auto text = test::read(profileFixture("mingw-gcc14-cpp23.toml")), snapshot = test::read(profileFixture("mingw-predefined.txt"));
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
  auto text = test::read(profileFixture("mingw-gcc14-cpp23.toml"));
  auto r = parseProfile(text, "mingw-gcc14-cpp23.toml"); CHECK(!r.ok()); CHECK(r.error().message.find("snapshot missing") != std::string::npos);
  auto builtin = lookupProfile("mingw-gcc14-cpp23", {}, {{"mingw-gcc14-cpp23.toml", text}});
  CHECK(!builtin.ok()); if (!builtin.ok()) CHECK(builtin.error().message.find("snapshot missing") != std::string::npos);
  auto p = synthetic(text).value(); CHECK(p.checkA3({"LOCAL", "GUARD"}).ok());
  auto conflict = p.checkA3({"ONLINE_JUDGE", "WIN32"}); CHECK(!conflict.ok());
  CHECK(conflict.error().message.find("ONLINE_JUDGE") != std::string::npos); CHECK(conflict.error().message.find("WIN32") != std::string::npos);
}
TEST(profile_lookup_user_directory_environment_embedded_and_path_forms) {
  test::TempDirectory temp;
  auto text = allowProfile();
  ProfileFiles files{{"mingw-gcc14-cpp23.toml", replace(text, "Synthetic MinGW fixture", "Embedded fixture")}};
  ProfileLookup lookup; lookup.userDirectory = temp.path;
  auto builtin = lookupProfile("mingw-gcc14-cpp23", lookup, files); CHECK(builtin.ok()); CHECK_EQ(builtin.value().description, "Embedded fixture");
  temp.write("mingw-gcc14-cpp23.toml", text);
  auto user = lookupProfile("mingw-gcc14-cpp23", lookup, files); CHECK(user.ok()); CHECK_EQ(user.value().description, "Synthetic MinGW fixture");
  auto path = lookupProfile(pathToUtf8(temp.path / "mingw-gcc14-cpp23.toml"), {}, files); CHECK(path.ok()); CHECK_EQ(path.value().provenance.file, temp.path / "mingw-gcc14-cpp23.toml");
  temp.write("mingw-gcc14-cpp23.toml", text + "\ntypo = true"); CHECK(!lookupProfile("mingw-gcc14-cpp23", lookup, files).ok());
  CHECK(!lookupProfile("absent", lookup, files).ok()); CHECK(!lookupProfile("BAD", lookup, files).ok());
  CHECK(!lookupProfile("mingw-gcc14-cpp23.toml", lookup, files).ok()); // Path form does not search by name.
  auto snapshotFiles = ProfileFiles{{"mingw-gcc14-cpp23.toml", test::read(profileFixture("mingw-gcc14-cpp23.toml"))}, {"mingw-predefined.txt", test::read(profileFixture("mingw-predefined.txt"))}};
  auto embedded = lookupProfile("mingw-gcc14-cpp23", {}, snapshotFiles); CHECK(embedded.ok()); CHECK(embedded.value().reproduced);
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
  std::ofstream(config / "mingw-gcc14-cpp23.toml") << allowProfile();
  lookup.userDirectory.reset(); lookup.environment["APPDATA"] = pathToUtf8(temp.path);
  CHECK(lookupProfile("mingw-gcc14-cpp23", lookup, {}).ok());
  lookup.windows = false; lookup.environment["XDG_CONFIG_HOME"] = pathToUtf8(temp.path);
  CHECK(lookupProfile("mingw-gcc14-cpp23", lookup, {}).ok());
}

TEST(profile_file_errors_and_relative_snapshot_lookup) {
  test::TempDirectory temp;
  CHECK(!loadProfile(temp.path / "missing.toml").ok());
  CHECK(!loadProfile(temp.path).ok());
  temp.write("mingw-gcc14-cpp23.toml", test::read(profileFixture("mingw-gcc14-cpp23.toml")));
  auto missing = loadProfile(temp.path / "mingw-gcc14-cpp23.toml"); CHECK(!missing.ok());
  if (!missing.ok()) CHECK(missing.error().message.find("snapshot missing") != std::string::npos);
  temp.write("mingw-predefined.txt", test::read(profileFixture("mingw-predefined.txt")));
  CHECK(loadProfile(temp.path / "mingw-gcc14-cpp23.toml").ok());
  temp.write("mingw-predefined.txt", "# metadata\nnot a macro\n");
  auto malformed = loadProfile(temp.path / "mingw-gcc14-cpp23.toml"); CHECK(!malformed.ok());
  if (!malformed.ok()) CHECK(malformed.error().message.find("snapshot line 1") != std::string::npos);
}
TEST(profile_toml_schema_accepts_escaped_description_and_quoted_schema_keys) {
  auto text = replace(allowProfile(), "description = \"Synthetic MinGW fixture\"", "'description' = \"Line 1\\nLine 2\\t\\u03B1\"");
  auto result = synthetic(text); CHECK(result.ok());
  if (result.ok()) CHECK_EQ(result.value().description, "Line 1\nLine 2\tα");
}

TEST(profile_native_model_states_and_missing_records) {
  auto text = replace(test::read(profileFixture("mingw-gcc14-cpp23.toml")), "flags = [\"-DONLINE_JUDGE\"]", "flags = [\"-DONLINE_JUDGE\", \"-mtune=native\"]");
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
  auto text = test::read(profileFixture("mingw-gcc14-cpp23.toml")), snapshot = test::read(profileFixture("mingw-predefined.txt"));
  CHECK(!synthetic(text, replace(snapshot, "# command-operation #define ONLINE_JUDGE 1", "# command-operation #define ONLINE_JUDGE 2")).ok());
  CHECK(!synthetic(text, replace(snapshot, "# command-operation #define ONLINE_JUDGE 1", "#define ONLINE_JUDGE 1")).ok());
  CHECK(!synthetic(text, snapshot + "# command-operation #define UNDECLARED 1\n").ok());
  CHECK(synthetic(text + "\nlink=['-static','-Wl,--stack=268435456','-lstdc++exp']").ok());
  CHECK(!synthetic(text + "\nlink=['-DBAD=1']").ok());
  CHECK(!synthetic(text + "\nlink=['-O2']").ok());
}

TEST(profile_names_and_optional_alias_schema) {
  const auto text = allowProfile();
  for (const auto* name : {"mingw", "Mingw-gcc14-cpp23", "mingw_gcc14_cpp23", "mingw-clang14-cpp23",
                           "mingw-gcc-cpp23", "mingw-gcc14-c++23", "-mingw-gcc14-cpp23", "mingw-gcc14-cpp23-"}) {
    auto changed = replace(text, "mingw-gcc14-cpp23", name);
    auto parsed = parseProfile(changed, std::string(name) + ".toml");
    CHECK(!parsed.ok()); if (!parsed.ok()) CHECK(parsed.error().message.find("name:") != std::string::npos);
  }
  CHECK(parseProfile(replace(text, "mingw-gcc14-cpp23", "mingw-gcc14-cpp23-32bit"), "mingw-gcc14-cpp23-32bit.toml").ok());
  CHECK(!parseProfile(text, "different-gcc14-cpp23.toml").ok());
  for (const auto* alias : {"''", "'Upper'", "'bad_alias'", "'bad/alias'", "'c++23'", "true", "[]"})
    badProfile(text + "\nalias = " + alias, "alias:");
  auto valid = synthetic(text + "\nalias = 'mingw'");
  CHECK(valid.ok()); if (valid.ok()) CHECK_EQ(valid.value().alias.value(), "mingw");
}
TEST(profile_full_names_aliases_and_selection_line) {
  for (const auto& [alias, name] : {std::pair{"codeforces", "codeforces-gcc14-cpp23"}, {"atcoder", "atcoder-gcc15-gnupp23"}}) {
    auto shortName = lookupProfile(alias), fullName = lookupProfile(name);
    CHECK(shortName.ok()); CHECK(fullName.ok()); if (!shortName.ok() || !fullName.ok()) continue;
    CHECK_EQ(shortName.value().name, name);
    CHECK_EQ(fullName.value().alias.value(), alias);
    CHECK_EQ(shortName.value().provenance.origin, "bundled");
    CHECK_EQ(shortName.value().provenance.selector, alias);
    CHECK_EQ(shortName.value().provenance.contentDigest, fullName.value().provenance.contentDigest);
    CHECK_EQ(profileSelectionLine(shortName.value()), "judge: " + std::string(alias) + " -> " + name + "\n");
    CHECK_EQ(profileSelectionLine(fullName.value()), "judge: " + std::string(name) + " -> " + name + "\n");
  }
}
TEST(profile_alias_shadowing_and_user_identifier_collisions) {
  test::TempDirectory temp;
  ProfileLookup lookup; lookup.userDirectory = temp.path;
  const auto text = allowProfile();
  const ProfileFiles bundled{{"mingw-gcc14-cpp23.toml", text + "\nalias = 'judge'"}};
  auto userText = replace(text, "mingw-gcc14-cpp23", "custom-gcc14-cpp23") + "\nalias = 'judge'";
  temp.write("custom-gcc14-cpp23.toml", userText);
  for (int run = 0; run < 2; ++run) {
    auto p = lookupProfile("judge", lookup, bundled);
    CHECK(p.ok()); if (!p.ok()) continue;
    CHECK_EQ(p.value().name, "custom-gcc14-cpp23"); CHECK_EQ(p.value().provenance.origin, "user");
    CHECK_EQ(p.value().provenance.file, temp.path / "custom-gcc14-cpp23.toml");
    CHECK_EQ(p.value().warnings.size(), 1u);
    CHECK(p.value().warnings.front().find("shadows bundled identifier 'judge'") != std::string::npos);
  }
  auto pinned = lookupProfile("mingw-gcc14-cpp23", lookup, bundled);
  CHECK(pinned.ok()); if (pinned.ok()) CHECK_EQ(pinned.value().provenance.origin, "bundled");
  // A user filename also shadows a bundled full name.
  temp.write("mingw-gcc14-cpp23.toml", text);
  auto overridden = lookupProfile("mingw-gcc14-cpp23", lookup, bundled);
  CHECK(overridden.ok()); if (overridden.ok()) {
    CHECK_EQ(overridden.value().provenance.origin, "user");
    CHECK_EQ(overridden.value().warnings.size(), 2u);
  }
  // Duplicate aliases, and a user name claimed by another user's alias, fail
  // regardless of which identifier was requested (including list).
  for (const auto* alias : {"judge", "mingw-gcc14-cpp23"}) {
    temp.write("custom-gcc14-cpp23.toml", replace(userText, "alias = 'judge'", "alias = '" + std::string(alias) + "'"));
    if (std::string_view(alias) == "judge") temp.write("mingw-gcc14-cpp23.toml", text + "\nalias = 'judge'");
    else temp.write("mingw-gcc14-cpp23.toml", text);
    auto result = lookupProfile("mingw-gcc14-cpp23", lookup, bundled);
    CHECK(!result.ok()); if (!result.ok()) {
      CHECK(result.error().message.find("identifier collision '" + std::string(alias) + "'") != std::string::npos);
      CHECK(result.error().message.find("custom-gcc14-cpp23.toml") != std::string::npos);
      CHECK(result.error().message.find("mingw-gcc14-cpp23.toml") != std::string::npos);
    }
    CHECK(!listProfiles(lookup, bundled).ok());
  }
}
TEST(profile_cses_bundled_approximate_snapshot) {
  CHECK(embeddedProfileFiles().contains("cses-gcc13-cpp23.toml"));
  const auto alias = lookupProfile("cses"), full = lookupProfile("cses-gcc13-cpp23");
  CHECK(alias.ok()); CHECK(full.ok()); if (!alias.ok() || !full.ok()) return;
  CHECK_EQ(alias.value().name, "cses-gcc13-cpp23");
  CHECK_EQ(alias.value().provenance.contentDigest, full.value().provenance.contentDigest);
  CHECK(!alias.value().reproduced); CHECK(!alias.value().build);
  CHECK_EQ(alias.value().initialState("ONLINE_JUDGE").state, MacroState::undefined);
}
TEST(profile_qoj_bundled_approximate_snapshot) {
  CHECK(embeddedProfileFiles().contains("qoj-gcc15-cpp23.toml"));
  const auto alias = lookupProfile("qoj"), full = lookupProfile("qoj-gcc15-cpp23");
  CHECK(alias.ok()); CHECK(full.ok()); if (!alias.ok() || !full.ok()) return;
  const auto& p = alias.value();
  CHECK_EQ(p.name, "qoj-gcc15-cpp23");
  CHECK_EQ(p.provenance.contentDigest, full.value().provenance.contentDigest);
  CHECK(!p.reproduced); CHECK(!p.build);
  CHECK(p.snapshotCompiler.has_value());
  if (p.snapshotCompiler) CHECK_EQ(p.snapshotCompiler->second, "ubuntu-26.04:g++-15=15.2.0-16ubuntu1");
  CHECK_EQ(p.dialect.name, "c++23");
  CHECK_EQ(p.flags, (std::vector<std::string>{"-O2", "-DONLINE_JUDGE"}));
  CHECK_EQ(p.link, (std::vector<std::string>{"-lm"}));
  CHECK_EQ(p.initialState("ONLINE_JUDGE").state, MacroState::known);
  CHECK_EQ(p.initialState("ONLINE_JUDGE").definition->replacement, "1");
  CHECK_EQ(p.initialState("__cplusplus").definition->replacement, "202302L");
  CHECK_EQ(p.initialState("__unlisted_reserved").state, MacroState::unknown);
  CHECK_EQ(p.probeSourceBytes, 60000u); CHECK_EQ(p.probeOutputBytes, 60000u);
}
TEST(profile_kattis_bundled_approximate_snapshot) {
  CHECK(embeddedProfileFiles().contains("kattis-gcc15-gnupp23.toml"));
  const auto alias = lookupProfile("kattis"), full = lookupProfile("kattis-gcc15-gnupp23");
  CHECK(alias.ok()); CHECK(full.ok()); if (!alias.ok() || !full.ok()) return;
  const auto& p = alias.value();
  CHECK_EQ(p.name, "kattis-gcc15-gnupp23");
  CHECK_EQ(p.provenance.contentDigest, full.value().provenance.contentDigest);
  CHECK(!p.reproduced); CHECK(!p.build); CHECK(p.snapshot); CHECK(p.probeExpected.has_value());
  CHECK(p.snapshotCompiler.has_value());
  if (p.snapshotCompiler) {
    CHECK_EQ(p.snapshotCompiler->first, "image");
    CHECK_EQ(p.snapshotCompiler->second, "docker.io/kattis/judge-cpp@sha256:453998e7fefcf47fb86c094be4718f2986c0af3e7a079c7bd5696a60929d514d");
    CHECK_EQ(p.snapshotMetadata.snapshotCompiler, "image=" + p.snapshotCompiler->second);
  }
  CHECK_EQ(p.compiler.major, 15); CHECK_EQ(p.compiler.minor, 2); CHECK_EQ(p.compiler.patch, 0);
  CHECK_EQ(p.triple, "x86_64-linux-gnu");
  CHECK_EQ(p.dialect.name, "gnu++23");
  CHECK_EQ(p.flags, (std::vector<std::string>{"-g", "-O2"})); CHECK(p.inert.empty());
  CHECK_EQ(p.link, (std::vector<std::string>{"-static", "-lrt", "-Wl,-u,_IO_wfile_doallocate",
                                         "-Wl,--whole-archive", "-lpthread", "-Wl,--no-whole-archive"}));
  CHECK_EQ(p.libraries, (std::vector<std::string>{"libstdc++", "glibc"}));
  CHECK(p.macroOperations.empty()); CHECK(p.judgeMacros.empty());
  CHECK_EQ(p.initialState("ONLINE_JUDGE").state, MacroState::undefined);
  CHECK_EQ(p.initialState("__cplusplus").definition->replacement, "202302L");
  CHECK_EQ(p.initialState("__unlisted_reserved").state, MacroState::unknown);
  CHECK_EQ(p.probeSourceBytes, 60000u); CHECK_EQ(p.probeOutputBytes, 60000u);
}
TEST(profile_list_columns_and_order) {
  test::TempDirectory temp;
  ProfileLookup lookup; lookup.userDirectory = temp.path;
  temp.write("mingw-gcc14-cpp23.toml", allowProfile());
  auto catalog = listProfiles(lookup);
  CHECK(catalog.ok()); if (!catalog.ok()) return;
  const auto list = renderProfileList(catalog.value());
  CHECK(list.starts_with("atcoder-gcc15-gnupp23\tatcoder\tbundled\treproduced\t2026-11-15\t\"AtCoder"));
  CHECK(list.find("codeforces-gcc14-cpp23\tcodeforces\tbundled\tapproximate\t2026-11-15\t\"Codeforces") != std::string::npos);
  CHECK(list.find("mingw-gcc14-cpp23\t-\tuser " + quotedPath(temp.path / "mingw-gcc14-cpp23.toml") + "\tapproximate\t2026-11-15\t\"Synthetic MinGW fixture\"\t-\n") != std::string::npos);
  CHECK(list.find("qoj-gcc15-cpp23\tqoj\tbundled\tapproximate\t2026-11-17\t\"QOJ") != std::string::npos);
  CHECK(list.find("kattis-gcc15-gnupp23\tkattis\tbundled\tapproximate\t2026-11-17\t\"Kattis") != std::string::npos);
  const auto count = 6;
  CHECK_EQ(std::count(list.begin(), list.end(), '\n'), count);
  catalog.value().profiles.back().description = "Line 1\nLine 2\tmore";
  const auto escaped = renderProfileList(catalog.value());
  CHECK_EQ(std::count(escaped.begin(), escaped.end(), '\n'), count);
}
TEST(profile_digest_sha256_vectors_and_exact_input_bytes) {
  CHECK_EQ(detail::sha256(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK_EQ(detail::sha256("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK_EQ(detail::sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
           "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  CHECK_EQ(lookupProfile("codeforces").value().provenance.contentDigest,
           "95171947eee3866811e9991b7bb409663dd3bbf2a26583ab4901f45723dc1668");
  CHECK_EQ(lookupProfile("atcoder").value().provenance.contentDigest,
           "87f950dd750954b4c12f0636eeb351083803d5e512cb624b2dcef2ae5536e205");
  const auto text = test::read(profileFixture("mingw-gcc14-cpp23.toml"));
  const auto snapshot = test::read(profileFixture("mingw-predefined.txt"));
  auto p = synthetic(text); CHECK(p.ok()); if (!p.ok()) return;
  // Independently framed per the documented rule; path and selector are excluded.
  auto frame = [](const std::string& bytes) { return std::to_string(bytes.size()) + ":" + bytes; };
  const auto expected = detail::sha256("distill-profile-v1\n" + frame(text) + frame("mingw-predefined.txt") + frame(snapshot));
  CHECK_EQ(p.value().provenance.contentDigest, expected);
  auto moved = parseProfile(text, "elsewhere/mingw-gcc14-cpp23.toml", [&](const auto&) -> Result<std::string> { return snapshot; });
  CHECK(moved.ok()); if (moved.ok()) CHECK_EQ(moved.value().provenance.contentDigest, expected);
  CHECK(synthetic(text + "\n# byte change\n").value().provenance.contentDigest != expected);
  CHECK(synthetic(text, snapshot + "\n").value().provenance.contentDigest != expected);
  CHECK(synthetic(allowProfile()).value().provenance.contentDigest != expected);
}
TEST(profile_report_selection_origin_path_and_digest) {
  auto p = lookupProfile("codeforces"); CHECK(p.ok()); if (!p.ok()) return;
  const auto bundled = renderReport(PipelineResult{}, CompileCommand{}, &p.value());
  CHECK(bundled.find("judge: codeforces -> codeforces-gcc14-cpp23\n") != std::string::npos);
  CHECK(bundled.find("judge selector: \"codeforces\"\njudge resolved name: codeforces-gcc14-cpp23\njudge origin: bundled\njudge digest: sha256:" + p.value().provenance.contentDigest) != std::string::npos);
  test::TempDirectory temp;
  temp.write("mingw-gcc14-cpp23.toml", allowProfile());
  auto user = lookupProfile(pathToUtf8(temp.path / "mingw-gcc14-cpp23.toml")); CHECK(user.ok()); if (!user.ok()) return;
  const auto report = renderReport(PipelineResult{}, CompileCommand{}, &user.value());
  CHECK(report.find("judge selector: " + quotedPath(temp.path / "mingw-gcc14-cpp23.toml")) != std::string::npos);
  CHECK(report.find("judge resolved name: mingw-gcc14-cpp23\njudge origin: user " + quotedPath(temp.path / "mingw-gcc14-cpp23.toml")) != std::string::npos);
  CHECK(report.find("judge digest: sha256:" + user.value().provenance.contentDigest) != std::string::npos);
}
TEST(profile_probe_budgets_strict_schema_and_expected_excluded_from_digest) {
  const auto base = test::read(profileFixture("mingw-gcc14-cpp23.toml"));
  auto good = synthetic(base + "\nprobe_source_bytes=70000\nprobe_output_bytes=80000\n");
  CHECK(good.ok());
  if (good.ok()) { CHECK_EQ(good.value().probeSourceBytes,70000u); CHECK_EQ(good.value().probeOutputBytes,80000u); }
  for (const auto& value : {"'60000'", "true", "0", "-1", "1023", "16777217", "[]"})
    badProfile(base + "\nprobe_source_bytes=" + value + "\n", "probe_source_bytes");
  badProfile(base + "\nprobe_output_bytes=false\n", "probe_output_bytes");
  auto load = [&](std::string expected) {
    return parseProfile(base,"mingw-gcc14-cpp23.toml", [&](const auto& path) -> Result<std::string> {
      if (path.filename() == "mingw-predefined.txt") return test::read(profileFixture("mingw-predefined.txt"));
      return expected;
    });
  };
  auto a=load("expected A"), b=load("expected B"); CHECK(a.ok()); CHECK(b.ok());
  CHECK_EQ(a.value().provenance.contentDigest,b.value().provenance.contentDigest);
  CHECK_EQ(a.value().probeExpected.value(),"expected A");
}
TEST(profile_list_embeds_observations_for_exact_digest) {
  test::TempDirectory temp;
  ProfileLookup lookup; lookup.userDirectory=temp.path;
  const auto text=allowProfile();
  auto parsed=parseProfile(text,"mingw-gcc14-cpp23.toml"); CHECK(parsed.ok());
  ProfileFiles files{{"mingw-gcc14-cpp23.toml",text}};
  auto record=[&](std::string digest, std::string status) {
    return "distill-observation: 1\nprofile: mingw-gcc14-cpp23\ndigest: "+digest+"\nstatus: "+status+"\n";
  };
  files["observations/mingw-gcc14-cpp23-2026-11-16-2.txt"]=record(parsed.value().provenance.contentDigest,"user-reported match");
  files["observations/mingw-gcc14-cpp23-2026-11-16-10.txt"]=record(parsed.value().provenance.contentDigest,"user-reported failure");
  files["observations/mingw-gcc14-cpp23-2026-11-17-1.txt"]=record("other digest","user-reported match");
  auto catalog=listProfiles(lookup,files); CHECK(catalog.ok());
  CHECK(renderProfileList(catalog.value()).find("-2026-11-16-10.txt: user-reported failure")!=std::string::npos);
}
