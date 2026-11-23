// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Base {
    Base() = default;
    Base& operator=(int) { return *this; }
};
struct Derived : Base { Derived() = default; };
int main() {
    Derived d;
    int result = __is_assignable(Derived&, int);
    std::printf("%d\n", result);
    return result;
}
