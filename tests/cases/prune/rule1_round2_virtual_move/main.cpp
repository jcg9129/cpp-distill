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
struct Marker {
    Marker& operator=(const Marker&) { observed = 1; return *this; }
    Marker& operator=(Marker&&) { observed = 2; return *this; }
};
struct Derived : virtual Base { Marker marker; };
int main() {
    Derived a, b;
    a = static_cast<Derived&&>(b);
    std::printf("%d\n", observed);
    return observed;
}
