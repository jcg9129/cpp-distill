// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct X {
    X(long) {}
    X(unsigned long) {}
};
int main() {
    X x(0L);
    int result = __is_assignable(X&, int);
    std::printf("%d\n", result);
    return result;
}
