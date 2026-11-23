// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Arg {
  int operator+(long) { return 1; }
  int operator+(unsigned long) { return 2; }
};
struct Target {
  template<class T> requires requires(T t) { t + 0; }
  Target(T) {}
};
int main() {
  Arg arg;
  const bool accepted = __is_constructible(Target, Arg);
  std::printf("%d %d\n", arg + 0L, int(accepted));
}
