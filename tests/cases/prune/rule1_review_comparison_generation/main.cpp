// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <compare>
struct X {
    int value;
    operator int() const { return 0; }
    bool operator==(void*) const { return false; }
    auto operator<=>(const X&) const = default;
};
int main() {
    X a{1}, b{2};
    int result = a == b ? 0 : 1;
    std::printf("%d\n", result);
    return result;
}
