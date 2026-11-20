// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <type_traits>
struct Noisy {
  int member = 0;
  constexpr ~Noisy() { if (!std::is_constant_evaluated()) std::puts("destroy"); }
};
struct Quiet { int member = 0; };
#ifdef PICK
#ifndef NOISY_TARGET
#define NOISY_TARGET (sizeof(long)==4)
#endif
template<bool> struct Pick { using type = Quiet; };
template<> struct Pick<true> { using type = Noisy; };
using Temporary = Pick<NOISY_TARGET>::type;
#else
using Temporary = Noisy;
#endif
#ifndef DROP_TRIGGER
template<int> constexpr const int& value = Temporary{}.member;
const int* dormant() { return &value<0>; }
#endif
int main() { std::puts("main"); }
