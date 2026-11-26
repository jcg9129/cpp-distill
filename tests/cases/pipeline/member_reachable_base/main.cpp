// SPDX-License-Identifier: GPL-3.0-or-later
#include <vector>
struct Base { int init() { return 9; } };
struct Derived : virtual Base {};
int main() { std::vector<Derived> c(1); return c.empty(); }
