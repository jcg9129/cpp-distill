// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Arg {
    Arg(long) {}
    Arg(unsigned long) {}
};
struct Outer { Arg value; };
int main() {
    Arg a(0L);
    Outer o{a};
    int result = __is_constructible(Outer, int);
    std::printf("%d\n", result);
    return result;
}
