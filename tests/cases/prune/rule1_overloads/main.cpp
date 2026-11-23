// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <type_traits>
int ctor_helper() { return 81; }
int operator_helper() { return 82; }
// These names are global rule-2 blockers for ordinary methods. They must not
// turn constructor/operator declaration identities into member-name lookups.
struct VirtualNames {
  virtual int operator()() { return 0; }
  virtual void Overloads() {}
};
struct Overloads {
  int value = 0;
  Overloads() = default;
  Overloads(int a, int b) : value(a+b) {}
  Overloads(int, double) { value = ctor_helper(); }
  explicit Overloads(char) { value = 83; }
  Overloads(float) { value = 84; }
  int operator+(int n) const { return value+n; }
  int operator+(double) const { return operator_helper(); }
  int operator()() const { return 85; }
  bool operator==(const Overloads&) const { return false; }
  int operator[](int) const { return 86; }
};
struct Shared { int operator+(int), operator-(int); };
struct Sole { Sole(int, double) {} };
struct Copies { Copies() = default; Copies(const Copies&) {} };
struct Checked {
  Checked() = default;
  Checked(int, double) {}
  int operator+(int) const { return 87; }
};
static_assert(std::is_constructible_v<Checked,int,double>);
template<class T> struct Box { T value; };
Box(long) -> Box<long>;
int main() {
  Overloads x(2,3);
  Sole* s = nullptr;
  Shared shared; (void)shared;
  Copies c;
  Box<int> b{7};
  std::printf("%d\n", x+2+b.value+(s!=nullptr)+int(sizeof(c))-1);
}
