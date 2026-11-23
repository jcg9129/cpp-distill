// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <type_traits>
struct F { int operator()(long) const {return 0;} int operator()(unsigned long) const {return 0;} };
namespace adl {
struct Holder { F* get(); };
template<class H> int probe(H h, decltype((*static_cast<H*>(nullptr)->get())(0))* = nullptr) {return 1;}
int probe(Holder, ...) {return 0;}
}
int main(){F f; std::printf("%d\n",f(0L)+probe(adl::Holder{},0));}
