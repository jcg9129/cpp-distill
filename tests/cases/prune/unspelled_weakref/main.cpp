// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Kept {};
Kept weakref_target(Kept) asm("weakref_target");
Kept weakref_target(Kept) { return {}; }
static Kept weakref_name(Kept) __attribute__((weakref("weakref_target")));
int main() { Kept k; (void)k; std::puts("ok"); }
