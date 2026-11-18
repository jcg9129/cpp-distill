// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.h"
#include "core/names/names.h"
using namespace distill;
TEST(names_identity_library_coverage) {
  for (const char* name : {"_MSC_VER", "__clang__", "__MINGW32__", "_WIN32"}) {
    CHECK(isIdentityMacro(name));
    CHECK(isIdentityMacro(name, {"glibc", "libstdc++"}));
    CHECK(isIdentityMacro(name, {"mingw-w64", "libstdc++"}));
    CHECK(!isIdentityMacro(name, {"unknown"}));
  }
  for (const char* name : {"_M_X64", "_M_AMD64", "_M_IX86", "_M_ARM", "_M_ARM64", "_M_ARM64EC", "_WIN64"}) {
    CHECK(isIdentityMacro(name, {"glibc", "libstdc++"}));
    CHECK(isIdentityMacro(name, {"macos-libc"}));
    CHECK(!isIdentityMacro(name));
    CHECK(!isIdentityMacro(name, {"mingw-w64", "libstdc++"}));
  }
  for (const char* name : {"_WIN32_WINNT", "__GLIBC__", "__cpp_lib_ranges", "_M_IA64", "__has_builtin"})
    CHECK(!isIdentityMacro(name, {"glibc"}));
}
TEST(names_standard_headers_through_cpp26_and_c23) {
  for (auto s : {"vector", "codecvt", "strstream", "meta", "contracts", "hive", "inplace_vector", "linalg", "hazard_pointer", "rcu", "simd", "debugging", "stdfloat", "ccomplex", "stdatomic.h", "stdbit.h", "stdckdint.h", "threads.h", "bits/stdc++.h", "ext/pb_ds/assoc_container.hpp", "ext/pb_ds/detail/anything.hpp", "ext/rope"}) CHECK(isStandardHeader(s));
  for (auto s : {"user.h", "Vector", "<vector>", "ext/pb_ds", "ext/rope_extra", "sys/types.h"}) CHECK(!isStandardHeader(s));
}
TEST(names_macro_families) {
  for (auto s : {"signal", "wcstod", "L_tmpnam_s", "PRIcMAX", "assert", "errno", "NULL", "EOF", "CHAR_BIT", "INT_MAX", "SIZE_MAX", "EXIT_SUCCESS", "SIGINT", "bool", "true", "false", "static_assert", "alignas", "offsetof", "setjmp", "va_start", "and", "or_eq", "I", "complex", "math_errhandling", "isgreater", "PRIdFAST32", "SCNxPTR", "PRIuLEAST24", "UINT24_MAX", "INT64_C", "LC_ALL", "ENOTRECOVERABLE", "FE_DFL_ENV", "FLT_TRUE_MIN", "DEC128_SNAN", "FP_FAST_D32ADDD128", "stdin", "TIME_UTC", "WEOF", "char8_t", "ATOMIC_FLAG_INIT", "atomic_fetch_add_explicit", "ONCE_FLAG_INIT", "stdc_bit_ceil", "ckd_mul", "memcpy", "printf", "wmemmove_s"}) CHECK(isStandardMacroName(s));
  CHECK(!isStandardMacroName("MY_GUARD")); CHECK(!isStandardMacroName("INTbanana_MAX"));
}
TEST(names_reserved_rule) {
  for (auto s : {"__GNUC__", "_Upper", "some__name", "x__"}) CHECK(isReserved(s));
  for (auto s : {"", "_", "_lower", "MY_GUARD", "a_B"}) CHECK(!isReserved(s));
}
TEST(names_extension_injection_and_library_union) {
  NameLists names;
  names.extensions = {{"fixture", {"EXTENSION"}}, {"other", {"OTHER_EXTENSION"}}};
  CHECK(names.protectedUserMacro("MY_GUARD"));
  CHECK(!names.protectedUserMacro("INT_MAX")); CHECK(!names.protectedUserMacro("__guard"));
  names.extensions["fixture"].insert("MY_GUARD");
  CHECK(!names.protectedUserMacro("MY_GUARD"));
  CHECK(!names.protectedUserMacro("MY_GUARD", {"fixture"}));
  CHECK(names.protectedUserMacro("MY_GUARD", {"other"}));
}
TEST(names_standard_headers_reject_path_escape_components) {
  for (auto s : {"/vector", "/ext/pb_ds/a.h", "ext/pb_ds/../../x.h", "ext/pb_ds/../x.h",
                 "ext/pb_ds/..", "ext/pb_ds//../x.h", "../vector"}) CHECK(!isStandardHeader(s));
  for (auto s : {"ext/pb_ds/detail/x.h", "ext/pb_ds/.../x.h", "ext/pb_ds/..x.h"}) CHECK(isStandardHeader(s));
#ifdef _WIN32
  for (auto s : {"C:vector", "c:/ext/pb_ds/a.h", "ext/pb_ds/..\\x.h", "ext/pb_ds/a\\b.h"}) CHECK(!isStandardHeader(s));
#endif
}

TEST(names_embedded_extensions_and_missing_inventory) {
  NameLists names;
  for (const char* library : {"glibc", "libstdc++", "mingw-w64", "macos-libc"}) {
    CHECK(names.extensionListReady({library}));
    CHECK(!names.extensions.at(library).empty());
    for (const char* name : {"MOD", "INF", "DEBUG", "LOCAL", "N", "MAXN", "rep", "ll"})
      CHECK(!names.extensions.at(library).contains(name));
  }
  CHECK(names.extensionListReady());
  CHECK(names.protectedUserMacro("DISTILL_UNLIKELY_GUARD_712", {"mingw-w64"}));
  CHECK(names.protectedUserMacro("DISTILL_UNLIKELY_GUARD_712", {"macos-libc"}));
  CHECK(!names.protectedUserMacro("DISTILL_UNLIKELY_GUARD_712", {"missing"}));
  CHECK(names.protectedUserMacro("DISTILL_UNLIKELY_GUARD_712"));
  CHECK(names.protectedUserMacro("DISTILL_UNLIKELY_GUARD_712", {"libstdc++"}));
  for (const char* library : {"glibc", "mingw-w64", "macos-libc"})
    CHECK(names.extensions.at(library).contains("M_PI"));
  CHECK(!names.protectedUserMacro("M_PI", {"glibc"}));
  CHECK(!names.protectedUserMacro("M_PI", {"glibc", "libstdc++"}));
  CHECK(!names.protectedUserMacro("PB_DS_ASSERT_VALID", {"libstdc++"}));
}
TEST(names_extension_parser_errors_and_empty) {
  CHECK(parseExtensionList("# metadata\n", "list").value().empty());
  CHECK(parseExtensionList("A\r\nZ\r\n", "list").ok());
  for (const auto* data : {"A\nA\n", "Z\nA\n", "__X\n", "_X\n", "assert\n", "INT24_MAX\n", "A B\n", "2BAD\n", " A\n", "caf\xc3\xa9\n"}) {
    auto result = parseExtensionList(data, "list"); CHECK(!result.ok());
    if (!result.ok()) CHECK(result.error().message.starts_with("list:"));
  }
}

TEST(names_embedded_inventory_matches_reviewed_files_exactly) {
  NameLists names;
  ExtensionLists raw;
  for (const char* library : {"glibc", "libstdc++", "mingw-w64", "macos-libc"}) {
    const auto path = std::filesystem::path(DISTILL_EXTENSION_DATA) / (std::string(library) + ".txt");
    auto parsed = parseExtensionList(test::read(path), pathToUtf8(path));
    if (!parsed.ok()) throw std::runtime_error(parsed.error().message);
    CHECK(!parsed.value().empty());
    raw.emplace(library, std::move(parsed.value()));
  }
  CHECK_EQ(names.extensions.at("glibc"), raw.at("glibc"));
  for (const char* library : {"libstdc++", "mingw-w64", "macos-libc"}) CHECK_EQ(names.extensions.at(library), raw.at(library));
}

TEST(names_multiple_selected_libraries_intersection_and_empty_fail_closed) {
  NameLists names; names.extensions = {{"a", {"M_PI"}}, {"b", {"EXT"}}, {"pending", {}}};
  CHECK(!names.protectedUserMacro("M_PI", {"a", "b"}));
  CHECK(!names.protectedUserMacro("EXT", {"a", "b"}));
  CHECK(names.protectedUserMacro("MY_GUARD", {"a", "b"}));
  CHECK(!names.protectedUserMacro("MY_GUARD", {"a", "pending"}));
  CHECK(!names.protectedUserMacro("MY_GUARD", {"a", "absent"}));
  CHECK(!names.protectedUserMacro("MY_GUARD"));
}
