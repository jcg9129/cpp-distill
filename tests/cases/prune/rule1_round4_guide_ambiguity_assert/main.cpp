// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
template<class T> struct B { template<class U> B(U) {} };
B(long) -> B<int>;
B(unsigned long) -> B<int>;
template<class T> concept CanDeduce = requires(T x) { B{x}; };
static_assert(!CanDeduce<int>);
int main() { B selected{0L}; std::printf("%d\n", int(CanDeduce<int>)); }
