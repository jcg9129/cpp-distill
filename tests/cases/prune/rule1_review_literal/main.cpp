// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct X {
    X() {}
    constexpr X(int) {}
};
constexpr int f(X) { return 0; }
int main() {
    X x;
    int result = f(x);
    std::printf("%d\n", result);
    return result;
}
