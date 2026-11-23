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
struct Derived { Base values[2]; };
int main() {
    std::printf("%d\n", int(sizeof(Derived) == 0));
    return 0;
}
