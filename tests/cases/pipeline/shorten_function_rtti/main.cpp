// SPDX-License-Identifier: GPL-3.0-or-later
#include <functional>
#include <cstdio>
struct LongUserCallable { int operator()() const { return 7; } };
int main() { std::function<int()> f = LongUserCallable{}; std::puts(f.target_type().name()); }
