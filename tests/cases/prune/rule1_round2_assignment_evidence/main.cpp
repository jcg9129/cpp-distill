// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Base {
    Base() = default;
    Base& operator=(int) { return *this; }
};
struct Holder {
    Base value;
    Holder() = default;
    Holder(const Base&) {}
};
int main() {
    Holder h;
    int result = __is_assignable(Holder&, int);
    std::printf("%d\n", result);
    return result;
}
