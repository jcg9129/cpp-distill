// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
using Scalar = unsigned;
constexpr Scalar make() { return 42; }
template<int> constexpr Scalar value = make();
Scalar dormant() { return value<0>; }
int main() { std::puts("main"); }
