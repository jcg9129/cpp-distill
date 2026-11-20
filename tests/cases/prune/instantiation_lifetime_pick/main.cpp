// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <type_traits>
struct Noisy {
  int member = 0;
  constexpr ~Noisy() { if (!std::is_constant_evaluated()) std::puts("destroy"); }
};
template<bool> struct Pick { struct type { int member = 0; }; };
template<> struct Pick<true> { using type = Noisy; };
template<int> constexpr const int& value = Pick<(sizeof(long)==4)>::type{}.member;
const int* dormant() { return &value<0>; }
int main() { std::puts("main"); }
