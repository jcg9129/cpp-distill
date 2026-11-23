// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <initializer_list>
struct Arg {
  Arg(long) {}
  Arg(unsigned long) {}
};
struct Outer {
  Outer(std::initializer_list<Arg>) {}
};
template<class T> concept CanListConstruct = requires { T{1}; };
int main() {
  Arg a(0L);
  Outer* o = nullptr;
  int result = CanListConstruct<Outer>;
  std::printf("%d\n", result + (o != nullptr));
  return result;
}
