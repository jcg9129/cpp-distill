// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
template<class T> inline int registration = std::printf("registered\n");
struct Unused { int dormant() { return registration<int>; } };
int main() { std::printf("main\n"); }
