// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct ArrayArg {
    ArrayArg() = default;
    ArrayArg(long) {}
    ArrayArg(unsigned long) {}
};
struct RefArg {
    RefArg() = default;
    RefArg(long) {}
    RefArg(unsigned long) {}
};
struct Outer {
    union { struct { ArrayArg values[2][2]; }; };
};
struct Ref { const RefArg& value; };
int main() {
    ArrayArg a(0L);
    RefArg b(0L);
    Outer o{};
    Ref r{b};
    int result = __is_constructible(Outer, int) || __is_constructible(Ref, int);
    std::printf("%d\n", result);
    return result;
}
