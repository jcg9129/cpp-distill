// SPDX-License-Identifier: GPL-3.0-or-later
#if defined(__BRANCH_UNKNOWN__)
#include "b.hpp"
#else
#include "c.hpp"
#endif
static_assert(__LINE__ == 7);
static_assert(__builtin_strcmp(__FILE__, "main.cpp") == 0);
#if 1
int resolved = __LINE__;
#endif
int main(){return which() != 3 || resolved != 10 || __LINE__ != 12;}
