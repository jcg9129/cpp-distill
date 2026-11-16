// SPDX-License-Identifier: GPL-3.0-or-later
#include <vendor.h>
#if __UNKNOWN
#if 0
#error dead
#endif
static_assert(__LINE__ == 7);
int captured = __builtin_LINE();
#else
#if 1
static_assert(__LINE__ == 11);
int captured = __builtin_LINE();
#endif
#endif
int main(){return captured != 8;}
