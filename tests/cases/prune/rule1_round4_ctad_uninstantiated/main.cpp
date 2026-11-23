// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
template<class T> struct A {T value;};
A(const char*) -> A<const char*>;
template<class T> void unused(T x) { (void)A{x}; }
int main(){A<int> a{0};std::printf("%d\n",a.value);}
