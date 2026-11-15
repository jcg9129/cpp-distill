// SPDX-License-Identifier: GPL-3.0-or-later
extern "C" int printf(const char*, ...);
#if 0
int x; /* not a directive
*/ #include "missing.h"
#endif
/* prefix
*/ #include /* operand
*/ "a.h" /* ignored tail
*/ int ignored_user;
constexpr int after_user = __LINE__;
#define X /* replacement
*/ 1
#define Y /* multiple
comments */ X /* still
replacement */ + 2
#include "once.h" /* once tail
*/ int ignored_once;
#include "once.h"
#include "system.h" /* rewrite tail
*/ int ignored_system;
constexpr int after_system = __LINE__;
#if 0
#include "a.h" /* skipped tail
*/ ignored_skipped
#elif /* switch
*/ 1 /* switch tail
*/ + 0
constexpr int after_elif = __LINE__;
#else /* inactive switch
*/ ignored_else
#endif /* close
*/ ignored_endif
constexpr int after_endif = __LINE__;
#if 1
#include "empty.h"
#elif /* inactive resync
*/ 0
#endif /* active close
*/ ignored_close
constexpr int after_close = __LINE__;
int main() {
  printf("%d %d %d %d %d %d %d %s\n", X, Y, value, once_value,
         after_user, after_system, after_elif + after_endif + after_close, __FILE__);
  return Y != 3 || system_value != 9;
}
