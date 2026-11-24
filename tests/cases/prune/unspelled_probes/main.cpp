// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Kept {};
template<class T> int requires_target(T) requires (sizeof(T) == 0);
template<class T> constexpr bool fails = requires(T t) { requires_target(t); };
static_assert(!fails<Kept>);
template<class T> auto decltype_target(T) -> typename T::missing;
template<class T> auto probe(T t) -> decltype(decltype_target(t), int{}) { return 1; }
int probe(...) { return 0; }
template<class T> int noexcept_target(T) noexcept(false) { return 0; }
static_assert(!noexcept(noexcept_target(Kept{})));
int main() { if (probe(Kept{}) != 0) return 1; std::puts("ok"); }
