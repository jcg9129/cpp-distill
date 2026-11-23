// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
template<class T> struct A { T value; };
template<class U> requires (sizeof(U) > sizeof(int))
A(U) -> A<U>;
template<class T> concept CanDeduce = requires(T x) { A{x}; };
int main() { A<int> a{0}; std::printf("%d\n", a.value + CanDeduce<int>); }
