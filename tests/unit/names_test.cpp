// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.h"
#include "core/names/names.h"
using namespace distill;
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
  CHECK(!names.protectedUserMacro("MY_GUARD", "fixture"));
  CHECK(names.protectedUserMacro("MY_GUARD", "other"));
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
  CHECK(names.extensionListReady("glibc")); CHECK(names.extensionListReady("libstdc++"));
  CHECK(!names.extensionListReady("mingw-w64")); CHECK(!names.extensionListReady("macos-libc"));
  CHECK(!names.protectedUserMacro("DISTILL_UNLIKELY_GUARD_712", "mingw-w64"));
  CHECK(!names.protectedUserMacro("DISTILL_UNLIKELY_GUARD_712", "macos-libc"));
  CHECK(!names.protectedUserMacro("DISTILL_UNLIKELY_GUARD_712", "missing"));
  CHECK(!names.protectedUserMacro("DISTILL_UNLIKELY_GUARD_712"));
  CHECK(names.protectedUserMacro("DISTILL_UNLIKELY_GUARD_712", "libstdc++"));
  CHECK(!names.protectedUserMacro("M_PI", "glibc"));
  CHECK(!names.protectedUserMacro("M_PI", "libstdc++"));
  CHECK(!names.protectedUserMacro("PB_DS_ASSERT_VALID", "libstdc++"));
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
    CHECK(parsed.ok()); if (!parsed.ok()) return;
    raw.emplace(library, std::move(parsed.value()));
  }
  CHECK_EQ(names.extensions.at("glibc"), raw.at("glibc"));
  raw.at("libstdc++").insert(raw.at("glibc").begin(), raw.at("glibc").end());
  for (const char* library : {"libstdc++", "mingw-w64", "macos-libc"}) CHECK_EQ(names.extensions.at(library), raw.at(library));
}
