// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
template<class T> struct Identity { using type = T; };
struct Arg {
  Arg(long) {}
  Arg(unsigned long) {}
};
struct Outer {
  template<class T = Arg> Outer& operator=(const typename Identity<T>::type&) { return *this; }
};
int main() {
  Arg a(0L);
  Outer o;
  o = a;
  int result = __is_assignable(Outer&, int);
  std::printf("%d\n", result);
  return result;
}
