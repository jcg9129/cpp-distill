// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <type_traits>
struct Noisy {
  int member = 0;
  constexpr ~Noisy() { if (!std::is_constant_evaluated()) std::puts("destroy"); }
};
template<int> inline const int& value = Noisy{}.member;
const int* dormant() { return &value<0>; }
int main() { std::puts("main"); }
