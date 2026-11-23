// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Base {
    Base& operator=(const Base&) = default;
    template<class T>
    Base& operator=(T&&) { return *this; }
    template<class T, int = 0>
    Base& operator=(T&&) { return *this; }
};
struct Derived : Base {
    Derived& operator=(const Derived&) = delete;
    Derived& operator=(Derived&&) = default;
};
int main() {
    int result = __is_assignable(Derived&, Derived&&);
    std::printf("%d\n", result);
    return result;
}
