// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
template<class T> struct A {T value;};
A(const char*) -> A<const char*>;
#define MAKE(T,x) T{x}
template<class T> concept Unused = requires(T x) { MAKE(A,x); };
int main(){A<int> a{0};std::printf("%d\n",a.value);}
