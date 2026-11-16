// SPDX-License-Identifier: GPL-3.0-or-later
#define JOIN_I(a,b) a##b
#define JOIN(a,b) JOIN_I(a,b)
struct Value { int data; };
struct Ancestor {
  using type = Value;
  static constexpr int value = 2;
  static Value e() { return {0}; }
  template<class U> static Value f(U n);
};
template<class U> Value Ancestor::f(U n) { return {int(n)}; }
struct Monoid : Ancestor { static Value op(Value a, Value b) { return {a.data+b.data}; } };
struct Traits { struct nested { using monoid = Monoid; }; };
struct Decoy { static int value() { return 0; } };
template<class T> int run() {
  using M = typename T::nested::monoid;
  typename M::type a = M::e();
  return M::JOIN(o,p)(a, M::template f<int>(M::value)).data;
}
int main() { return run<Traits>(); }
