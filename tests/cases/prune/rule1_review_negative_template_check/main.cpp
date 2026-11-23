// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
template<class T> struct Identity { using type = T; };
struct Arg {
  Arg(long) {}
  Arg(unsigned long) {}
};
struct Outer {
  template<class T = Arg> Outer(typename Identity<T>::type) {}
};
int main() {
  Arg a(0L);
  Outer* o = nullptr;
  (void)o;
  int result = __is_constructible(Outer, int);
  std::printf("%d\n", result);
  return result;
}
