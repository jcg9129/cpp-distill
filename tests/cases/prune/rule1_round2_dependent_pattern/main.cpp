// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Base {
    Base() = default;
    Base(long) {}
    Base(unsigned long) {}
    template<class T> Base& operator=(T&&) { return *this; }
};
template<class T> struct Holder { Base value; };
int main() {
    Holder<void>* pointer = nullptr;
    int result = pointer != nullptr;
    std::printf("%d\n", result);
    return result;
}
