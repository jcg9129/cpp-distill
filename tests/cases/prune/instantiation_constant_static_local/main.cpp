// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <type_traits>
struct Noisy {
  int member = 0;
  constexpr ~Noisy() { if (!std::is_constant_evaluated()) std::puts("destroy"); }
};
template<class T> void local() { static constexpr Noisy value{}; }
void dormant() { local<int>(); }
int main() { std::puts("main"); }
