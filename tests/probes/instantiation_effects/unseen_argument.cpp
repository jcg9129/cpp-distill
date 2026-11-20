// SPDX-License-Identifier: GPL-3.0-or-later
// CLASS_JUDGE models a target-dependent user expression such as sizeof(long)==4.
#ifndef CLASS_JUDGE
#define CLASS_JUDGE 0
#endif
#include <cstdio>
#include <type_traits>
struct Value {
  constexpr ~Value() { if (!std::is_constant_evaluated()) std::puts("destroy"); }
};
template<auto V> struct Object {};
template<class T, T V> struct DependentObject {};
template<class T> int dormant() {
  return sizeof(Object<T{}>) + sizeof(DependentObject<T, T{}>);
}
template<bool B> void select() { if constexpr (B) (void)dormant<Value>(); }
void trigger() {
  select<CLASS_JUDGE>();
#ifndef NO_KNOWN
  (void)sizeof(Object<1>);
  (void)sizeof(DependentObject<int, 1>);
#endif
}
int main() {}
