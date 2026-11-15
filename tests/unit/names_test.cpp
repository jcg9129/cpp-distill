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
  CHECK(names.protectedUserMacro("MY_GUARD"));
  CHECK(!names.protectedUserMacro("INT_MAX")); CHECK(!names.protectedUserMacro("__guard"));
  names.extensions["fixture"].insert("MY_GUARD");
  CHECK(!names.protectedUserMacro("MY_GUARD"));
  CHECK(!names.protectedUserMacro("MY_GUARD", "fixture"));
  CHECK(names.protectedUserMacro("MY_GUARD", "other"));
}
