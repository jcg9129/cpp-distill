// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Arg {
  Arg(long) {}
  Arg(unsigned long) {}
};
struct Outer {
  Outer(const Arg&) {}
};
template<class T> concept CanConstruct = requires { T(1); };
int main() {
  Arg a(0L);
  Outer o(a);
  int result = CanConstruct<Outer>;
  std::printf("%d\n", result);
  return result;
}
