// SPDX-License-Identifier: GPL-3.0-or-later
#include <tuple>
namespace app {
struct Pair { int a, b; };
template<unsigned I> int get(Pair p) { if constexpr (I == 0) return p.a; else return p.b; }
}
namespace std {
template<> struct tuple_size<app::Pair> : integral_constant<size_t, 2> {};
template<size_t I> struct tuple_element<I, app::Pair> { using type = int; };
}
int unused(){return 9;}
int main(){app::Pair x{2,3}; auto [a,b] = x; return a+b == 5 ? 0 : 1;}
