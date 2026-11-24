// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Kept {};
int implementation() { return 0; }
using Function = int (*)();
extern "C" Function resolver_target() { return &implementation; }
int indirect() __attribute__((ifunc("resolver_target")));
int main() { Kept k; (void)k; std::puts("ok"); }
