// SPDX-License-Identifier: GPL-3.0-or-later
#include <any>
#include <cstdio>
struct LongUserType {};
int main() { std::any object = LongUserType{}; std::puts(object.type().name()); }
