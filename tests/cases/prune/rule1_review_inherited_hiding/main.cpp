// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Base { Base(int) {} };
template<bool Explicit> struct Derived : Base {
  using Base::Base;
  int tag = 2;
  explicit(Explicit) Derived(int n) : Base(n) { tag = 3; }
  Derived(long n) : Base(static_cast<int>(n)) { tag = 1; }
};
int consume(Derived<true> x) { return x.tag; }
Derived<true> produce() { return 0; }
int main() {
  const int result = consume(0) + produce().tag;
  std::printf("%d\n", result);
  return result;
}
