// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct F {int operator()(long) const {return 0;} int operator()(unsigned long) const {return 0;}};
namespace associated {
struct Holder { F* get(); };
template<class H> auto probe(H h,int) -> decltype((*h.get())(0),int{}) {return 1;}
}
int probe(associated::Holder,...) {return 0;}
int main(){std::printf("%d\n",F{}(0L)+probe(associated::Holder{},0));}
