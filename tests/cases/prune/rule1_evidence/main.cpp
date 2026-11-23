// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct ConceptRecord {
  using type = int;
  ConceptRecord() = default;
  ConceptRecord(int, int) {}
  int operator+(int) { return 81; }
};
template<class T> concept HasType = requires { typename T::type; };
static_assert(HasType<ConceptRecord>);
struct RequiresRecord {
  RequiresRecord() = default;
  RequiresRecord(int, int) {}
  int operator+(int) { return 82; }
  int operator-(int) { return 83; }
};
static_assert(requires(RequiresRecord r) { r+1; });
struct Compared {
  int value;
  bool operator==(const Compared& b) const { return value == b.value; }
  bool operator!=(const Compared& b) const { return value != b.value; }
};
struct Outer {
  Compared c;
  bool operator==(const Outer&) const = default;
};
template<class T> struct Guided { T value; Guided(T x) : value(x) {} };
Guided(int) -> Guided<long>;
int main() {
  Outer a{{2}}, b{{3}};
  Guided g(4);
  std::printf("%d\n", int(a!=b)+int(g.value));
}
