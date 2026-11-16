// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#define JOIN_I(a,b) a##b
#define JOIN(a,b) JOIN_I(a,b)
int branch_helper(int){return 1;}
template<class T> int branch(T x){if constexpr(requires{JOIN(branch_,helper)(x);}) return JOIN(branch_,helper)(x); else return 0;}
int main(){return std::getchar()=='x' ? branch(0) : 0;}
