// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Kept {};
template<class T> Kept specialized(T) { return {}; }
template<> Kept specialized<int>(int) { return {}; }
extern "C" Kept c_linkage(Kept);
#define PRODUCE(name) Kept name(Kept) { return {}; }
PRODUCE(macro_function)
Kept attributed(Kept) __attribute__((weak));
Kept attributed(Kept) { return {}; }
Kept asm_named(Kept) asm("asm_named_symbol");
Kept asm_named(Kept) { return {}; }
Kept local_static(Kept) { static int x = std::puts("unreached"); return {}; }
int main() { Kept k; (void)k; std::puts("ok"); }
