// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Tag { friend auto supplied(Tag); };
template<class T> struct Inject { friend auto supplied(T) { return 7; } };
struct Unused { decltype(sizeof(Inject<Tag>)) dormant(); };
template<class T> concept Ready = requires(T t) { supplied(t); };
int main() { std::printf("%d\n", int(Ready<Tag>)); }
