// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Noisy { constexpr Noisy():value(42){} int value; ~Noisy(){std::puts("destroy");} };
template<int> inline const Noisy value;
const Noisy* dormant(){return &value<0>;}
int main(){std::puts("main");}
