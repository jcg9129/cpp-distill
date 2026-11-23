// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
template<class T> struct A {T value;};
A(const char*) -> A<const char*>;
template<class T> using Alias = A<T>;
template<class T> struct B { B(T) {} };
B(long) -> B<long>;
int main(){B<int> b{0}; A<int> a{0};std::printf("%d\n",a.value);}
