// SPDX-License-Identifier: GPL-3.0-or-later
#include <vector>
struct Pair { int a = 2, b = 3; int init() { return 4; } };
int main() { auto [a, b] = Pair{}; return a + b != 5; }
