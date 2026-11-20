// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
template<class T> struct Reg { static inline int x = std::printf("registered\n"); };
struct Unused { int dormant() { return Reg<int>::x; } };
int main() { std::printf("main\n"); }
