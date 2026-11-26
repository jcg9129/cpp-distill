// SPDX-License-Identifier: GPL-3.0-or-later
#include <vector>
struct First {};
struct Second { int init() { return 4; } };
template<class Base> struct Derived : Base {};
int main() {
  std::vector<Derived<First>> v(1);
  Derived<Second>* incomplete = nullptr;
  (void)incomplete;
  return v.empty();
}
