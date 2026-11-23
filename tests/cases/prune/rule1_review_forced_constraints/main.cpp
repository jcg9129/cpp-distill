// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
template<class T> constexpr bool gate() {
  static_assert(sizeof(T) != sizeof(int), "gate instantiated for int");
  return true;
}
struct Member {
  Member(int) {}
  template<class T = int> requires (gate<T>()) Member() {}
};
struct Outer { Member member; };
int main() { Outer x{Member(0)}; std::printf("0\n"); }
