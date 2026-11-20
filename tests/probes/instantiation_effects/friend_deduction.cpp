// SPDX-License-Identifier: GPL-3.0-or-later
template<class T> struct C {
  friend auto deduced(C const&) { return 7; }
};
auto deduced(C<int> const&);
#ifdef SEED
struct Dormant { decltype(sizeof(C<int>)) declaration(); };
#endif
template<class T> concept Qualified = requires(T* p) { (deduced)(*p); };
#include <cstdio>
int main() { std::printf("%d\n", int(Qualified<C<int>>)); }
