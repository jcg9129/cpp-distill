// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
template<class T> struct P {P(T){}};
template<class U> P(U*) -> P<const U*>;
int main(){P p{static_cast<int*>(nullptr)}; static_assert(__is_same(decltype(p),P<const int*>));std::printf("0\n");}
