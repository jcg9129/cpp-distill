// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Value {
  Value() = default;
  Value(int, int) {}
};
template<class T> struct Wrapper {
  template<class U> struct UnusedCheck {
    static constexpr bool value = __is_constructible(U,int,int);
  };
  T value;
};
int main() {
  Wrapper<Value> wrapper;
  std::printf("%d\n", int(sizeof(wrapper) > 0));
}
