// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Kept {};
Kept lambda_helper() { return [] { return Kept{}; }(); }
int main() { Kept k; (void)k; std::puts("ok"); }
