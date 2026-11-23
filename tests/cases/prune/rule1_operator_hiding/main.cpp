// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
int surrogate(int) { return 2; }
using Function = int(int);
struct B { int operator()(int) { return 1; } };
struct D : B {
  int operator()(long, long) { return 3; }
  int operator+(int) { return 12; }
  operator Function*() { return surrogate; }
};
struct Middle : virtual B {};
struct Transitive : Middle {
  int operator()(long, long) { return 4; }
  operator Function*() { return surrogate; }
};
struct TemplateHiding : B {
  template<class T> int operator()(T, T) { return 5; }
  operator Function*() { return surrogate; }
};
struct Imported : B { using B::operator(); };
struct UsingBase : Imported {
  int operator()(long, long) { return 6; }
  operator Function*() { return surrogate; }
};
struct UsingDerived : B {
  using B::operator();
  int operator()(long, long) { return 7; }
};
template<class Base> struct Dependent : Base {
  int operator()(long, long) { return 8; }
};
template<class Base> struct DependentUsing : Base {
  using Base::operator();
  int operator()(long, long) { return 9; }
};
struct Empty {};
struct Control : Empty {
  int operator()(long, long) { return 10; }
  Control& operator+=(int) { return *this; }
};
struct AssignBase { AssignBase& operator+=(int) { return *this; } };
struct AssignDerived : AssignBase { AssignDerived& operator+=(long) { return *this; } };
struct Dead : B { int operator()(long, long) { return 11; } };
int main() {
  B base; D d; Transitive t; TemplateHiding h; UsingBase b; UsingDerived u;
  AssignBase assigned; assigned += 0; AssignDerived hiddenAssignment;
  Dependent<Empty> e; DependentUsing<B> v; Control c;
  std::printf("%d\n", d(0)+t(0)+h(0)+b(0)+base(0));
  return 0;
}
