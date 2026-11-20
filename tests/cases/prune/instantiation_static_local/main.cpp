// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
template<class T> struct Effect {
  int run() { static int x = std::printf("called\n"); return x; }
};
struct Unused { int dormant() { return Effect<int>{}.run(); } };
int main() { std::printf("main\n"); }
