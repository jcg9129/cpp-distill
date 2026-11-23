// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct F {int operator()(long) const {return 0;} int operator()(unsigned long) const {return 0;}};
template<class T> struct A {template<class U> A(U){}};
template<class U> requires requires(U u){u(0);} A(U) -> A<int>;
A(...) -> A<long>;
int main(){A a{F{}}; std::printf("%d\n",F{}(0L)+!__is_same(decltype(a),A<long>));}
