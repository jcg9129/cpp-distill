// SPDX-License-Identifier: GPL-3.0-or-later
#include <type_traits>
struct FirstBase { int get() const { return 1; } static int e() { return 1; } };
struct SecondBase { int get() const { return 2; } static int e() { return 2; } };
struct First : FirstBase {};
struct Second : SecondBase {};
template<class A, class B> int select() {
  using T = std::conditional_t<sizeof(long) == 4, A, B>;
  return T{}.get() + T::e();
}
struct Common { int get() const { return 3; } static int e() { return 3; } };
struct Input { operator Common() const { return {}; } };
template<class A, class B> int common() {
  using T = std::common_type_t<A, B>;
  return T{}.get() + T::e();
}
struct Key {};
struct OutputBase { int get() const { return 4; } static int e() { return 4; } };
struct Output : OutputBase {};
namespace std { template<> struct common_type<Key, Key> { using type = Output; }; }
struct Unused { int get() const { return 0; } static int e() { return 0; } };
int main() { return select<First, Second>() + common<Input, Common>() + common<Key, Key>(); }
