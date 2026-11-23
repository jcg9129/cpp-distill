// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <type_traits>
struct Functor {
  int operator()(long) { return 1; }
  int operator()(unsigned long) { return 2; }
};
int main() {
  Functor f;
  std::printf("%d %d\n", f(0L), int(std::is_invocable_v<Functor,int>));
}
