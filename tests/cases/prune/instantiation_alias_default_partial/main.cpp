// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Tag { friend auto supplied(Tag); };
struct Outer {
  template<class T> struct Inject {};
  template<class T> struct Inject<T*> { friend auto supplied(T) { return 7; } };
};
using Alias = Outer::Inject<Tag*>;
template<class T = Alias> struct Default { T value; };
struct Unused { decltype(sizeof(Default<>)) dormant(); };
template<class T> concept Ready = requires(T t) { supplied(t); };
int main() { std::printf("%d\n", int(Ready<Tag>)); }
