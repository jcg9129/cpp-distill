// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Base {
    Base() = default;
    Base(long) {}
    Base(unsigned long) {}
    template<class T> Base& operator=(T&&) { return *this; }
};
struct Good { Base member; };
struct ExplicitDelete {
    ExplicitDelete() = default;
    ExplicitDelete(const ExplicitDelete&) = default;
    ExplicitDelete(ExplicitDelete&&) = default;
    Base member;
    ExplicitDelete& operator=(const ExplicitDelete&) = delete;
    ExplicitDelete& operator=(ExplicitDelete&&) = delete;
};
int main() {
    Base b(0L);
    Good g;
    ExplicitDelete d;
    std::printf("%d\n", int(sizeof(g) + sizeof(d) == 0));
    return 0;
}
