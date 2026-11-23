// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <type_traits>
struct Arg {
  Arg(long) {}
  Arg(unsigned long) {}
};
struct Outer {
  Outer(const Arg&) {}
};
template<class T> auto check(int) -> decltype(T(1), std::true_type{});
template<class> auto check(...) -> std::false_type;
int main() {
  Arg a(0L);
  Outer o(a);
  int result = decltype(check<Outer>(0))::value;
  std::printf("%d\n", result);
  return result;
}
