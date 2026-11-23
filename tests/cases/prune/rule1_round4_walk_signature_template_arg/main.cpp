// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct F { int operator()(long) const {return 0;} int operator()(unsigned long) const {return 0;} };

template<class T> struct Box { using functor=T; }; struct Holder { Box<F*> get(); };
template<class H> concept Accepted = requires(H h) { (*static_cast<typename decltype(h.get())::functor>(nullptr))(0); };
static_assert(!Accepted<Holder>);
int main(){F f; std::printf("%d\n",f(0L)+Accepted<Holder>);}
