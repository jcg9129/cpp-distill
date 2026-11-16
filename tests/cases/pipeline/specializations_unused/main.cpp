// SPDX-License-Identifier: GPL-3.0-or-later
#include <tuple>
#include <ranges>
struct UserRange { int* begin() const; int* end() const; };
namespace std {
template<> struct tuple_size<UserRange> : integral_constant<size_t, 0> {};
template<> struct tuple_element<0,UserRange> { using type = int; };
namespace ranges { template<> inline constexpr bool enable_borrowed_range<UserRange> = true; }
}
int main(){return 0;}
