// SPDX-License-Identifier: GPL-3.0-or-later
#define JOIN_I(a,b) a##b
#define JOIN(a,b) JOIN_I(a,b)
struct Result { int value() const { return 7; } };
struct Base {
  template<class U> Result merge(U) const;
};
template<class U> Result Base::merge(U) const { return {}; }
struct Derived : Base {};
struct Convertible { operator Result() const { return {}; } };
struct Leaf { Result read() const { return {}; } };
struct Arrow { Leaf* operator->() const { return nullptr; } };
struct Chain { Arrow operator->() const { return {}; } };
struct Decoy { template<class U> int merge(U) const { return 0; } int read() { return 0; } };
template<class T> int member(T x) { return x.template merge<int>(1).value(); }
template<class T> int convert(T x) { return x.operator Result().value(); }
inline int consume(Result x) { return x.value(); }
template<class T> int implicit_convert(T x) { return consume(x); }
template<class T> int arrow(T x) { return x->JOIN(re,ad)().value(); }
int main() { return member(Derived{}) + convert(Convertible{}) + implicit_convert(Convertible{}) + arrow(Chain{}); }
