// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
int notify() { return std::printf("called\n"); }
template<class T> struct Effect {
  int run() { static int x = notify(); return x; }
};
struct Unused { int dormant() { return Effect<int>{}.run(); } };
int main() { std::printf("main\n"); }
