// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Kept {};
Kept alias_target(Kept) { return {}; }
Kept alias_name(Kept) __attribute__((alias("_Z12alias_target4Kept")));
int main() { Kept k; (void)k; std::puts("ok"); }
