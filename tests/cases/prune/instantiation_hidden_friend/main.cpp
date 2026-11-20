// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
template<class T> struct S { friend bool operator==(S, S) { return true; } };
struct Unused { decltype(sizeof(S<int>)) dormant(); };
struct Dead {};
int main() { std::printf("%d\n", int(S<int>{} == S<int>{})); }
