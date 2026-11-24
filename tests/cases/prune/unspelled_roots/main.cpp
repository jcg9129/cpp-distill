// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Kept {};
Kept flag_root(Kept) { return {}; }
// distill: keep
Kept annotated(Kept) { return {}; }
extern "C++" { Kept cpp_linkage(Kept) { return {}; } }
int main() { Kept k; (void)k; std::puts("ok"); }
