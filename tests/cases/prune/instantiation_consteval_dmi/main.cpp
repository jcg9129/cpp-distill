// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Tag { friend auto supplied(Tag); };
template<class T> struct Inject { friend auto supplied(T) { return 7; } };
template<class T> consteval int evaluate() { return sizeof(Inject<T>); }
template<class T> struct Holder { int value = evaluate<T>(); };
struct Unused { void dormant() { Holder<Tag> h; (void)h; } };
template<class T> concept Ready = requires(T t) { supplied(t); };
int main() { std::printf("%d\n", int(Ready<Tag>)); }
