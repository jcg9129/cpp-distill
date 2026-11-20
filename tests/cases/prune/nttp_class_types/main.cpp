// SPDX-License-Identifier: GPL-3.0-or-later
#include <type_traits>
#include <cstdio>
struct Value { constexpr ~Value() {} };
template<Value V> struct ClassObject {};
using Chosen = std::conditional_t<(sizeof(long) > 4), int, Value>;
template<Chosen V> struct SystemSelected {};
struct Wrapper { Chosen member; };
template<Wrapper V> struct SystemSelectedMember {};
template<class T> using Identity = T;
using HiddenSelection = Identity<Chosen>;
template<HiddenSelection V> struct AliasSelected {};
template<bool B> struct Pick { using type=int; enum class mode { zero }; };
template<> struct Pick<false> { using type=Value; using mode=Value; };
template<Pick<(sizeof(long)>4)>::type V> struct UserSelected {};
template<Pick<(sizeof(long)>4)>::mode V> struct UserSelectedEnum {};
template<auto V> struct Placeholder {};
template<decltype(auto) V> struct DecltypePlaceholder {};
template<class T, T V> struct Dependent {};
template<class T> concept Any = true;
template<Any auto V> struct ConstrainedPlaceholder {};
struct Dormant {
  void unused() {
    (void)sizeof(ClassObject<Value{}>);
    (void)sizeof(SystemSelected<Chosen{}>);
    (void)sizeof(SystemSelectedMember<Wrapper{}>);
    (void)sizeof(AliasSelected<HiddenSelection{}>);
    (void)sizeof(UserSelected<Pick<(sizeof(long)>4)>::type{}>);
    (void)sizeof(UserSelectedEnum<Pick<(sizeof(long)>4)>::mode{}>);
    (void)sizeof(Placeholder<1>);
    (void)sizeof(DecltypePlaceholder<1>);
    (void)sizeof(Dependent<int, 1>);
    (void)sizeof(ConstrainedPlaceholder<1>);
  }
};
struct Dead {};
int main() { std::puts("ok"); }
