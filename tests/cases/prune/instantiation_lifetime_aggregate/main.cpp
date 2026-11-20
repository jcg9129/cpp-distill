// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <type_traits>
struct Noisy {
  int member = 0;
  constexpr ~Noisy() { if (!std::is_constant_evaluated()) std::puts("destroy"); }
};
struct Holder { const Noisy& member; };
static_assert(std::is_trivially_destructible_v<Holder>);
template<int> inline const Holder value{Noisy{}};
const Holder* dormant() { return &value<0>; }
int main() { std::puts("main"); }
