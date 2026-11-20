// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>
#include <cstdio>
using uint32 = std::uint32_t;
template<uint32 N> struct Scalar {};
template<uint32 N> using Alias = Scalar<N>;
template<uint32 N> int generic_lambda() {
  return []<uint32 P>() { return P; }.template operator()<N>();
}
enum class Mode { quick };
template<Mode M> struct Enumerated {};
struct Nontrivial { constexpr ~Nontrivial() {} };
template<Nontrivial* P> struct Pointer {};
template<Nontrivial& R> struct Reference {};
template<class T, T* P> struct DependentPointer {};
template<auto* P> struct AutoPointer {};
struct Trivial { int n; };
template<Trivial V> struct TrivialObject {};
struct Dormant {
  void unused() {
    (void)sizeof(Alias<3>);
    (void)generic_lambda<3>();
    (void)sizeof(Enumerated<Mode::quick>);
    (void)sizeof(Pointer<nullptr>);
    (void)sizeof(DependentPointer<Nontrivial, nullptr>);
    (void)sizeof(AutoPointer<static_cast<Nontrivial*>(nullptr)>);
    (void)sizeof(TrivialObject<Trivial{3}>);
  }
};
int main() { std::puts("ok"); }
