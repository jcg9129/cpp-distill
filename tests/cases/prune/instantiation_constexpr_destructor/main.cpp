// SPDX-License-Identifier: GPL-3.0-or-later
struct Noisy { constexpr ~Noisy(){} };
template<int> constexpr Noisy value{};
void dormant(){(void)&value<0>;}
int main(){}
