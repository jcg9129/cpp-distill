// SPDX-License-Identifier: GPL-3.0-or-later
struct Quiet { int member=0; };
struct Holder { const Quiet& member; };
template<int> constexpr Holder value{Quiet{}};
void dormant(){(void)&value<0>;}
int main(){}
