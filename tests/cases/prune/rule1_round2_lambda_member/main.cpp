// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
int observed = 0;
struct Base {
    Base& operator=(const Base&) = default;
    template<class T>
    Base& operator=(T&&) { return *this; }
    template<class T, int = 0>
    Base& operator=(T&&) { return *this; }
};
int main() {
    Base b;
    auto closure = [b] {};
    closure();
    std::printf("0\n");
}
