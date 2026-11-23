// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct X {
    X() = default;
    X(int) {}
};
int main() {
    X x{};
    int result = __is_aggregate(X);
    std::printf("%d\n", result);
    return result;
}
