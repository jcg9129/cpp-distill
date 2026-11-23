// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct X {
    operator int() const { return 0; }
    bool operator==(int) const { return false; }
    bool operator!=(int) const { return true; }
};
int main() {
    X x;
    bool forward = (x == 0);
    bool reverse = (0 == x);
    int result = forward ? 2 : reverse ? 0 : 1;
    std::printf("%d\n", result);
    return result;
}
