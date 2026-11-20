// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Noisy { constexpr Noisy():value(42){} int value; ~Noisy(){std::puts("destroy");} };
template<int> void local(){static const Noisy object;}
void dormant(){local<0>();}
int main(){std::puts("main");}
