// SPDX-License-Identifier: GPL-3.0-or-later
#include <concepts>
#include <type_traits>
#include <utility>
#include <cstdio>
struct F {int operator()(long) const {return 0;} int operator()(unsigned long) const {return 0;}};
template<class T> concept CallsInt = requires(T f){ f(0); };
template<class T> auto test(int) -> decltype(std::declval<T&>()(0),std::true_type{});
template<class> auto test(...) -> std::false_type;
static_assert(!std::is_invocable_v<F,int>);
static_assert(!std::invocable<F,int>);
static_assert(!CallsInt<F>);
static_assert(!decltype(test<F>(0))::value);
int main(){std::printf("%d\n",F{}(0L));}
