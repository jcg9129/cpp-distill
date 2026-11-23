// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Arg {
  Arg(long) {}
  Arg(unsigned long) {}
};
struct Destination {
  Destination& operator=(const volatile Arg&) { return *this; }
};
int main() {
  Arg a(0L);
  Destination d;
  d = a;
  int result = __is_assignable(Destination&, int);
  std::printf("%d\n", result);
  return result;
}
