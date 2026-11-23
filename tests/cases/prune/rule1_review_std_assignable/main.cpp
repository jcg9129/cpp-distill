// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <type_traits>
struct Arg {
  Arg(long) {}
  Arg(unsigned long) {}
};
struct Outer {
  Outer(const Arg&) {}
  Outer& operator=(const Arg&) { return *this; }
};
int main() {
  Arg a(0L);
  Outer o(a);
  int result = std::is_assignable_v<Outer&, int>;
  std::printf("%d\n", result);
  return result;
}
