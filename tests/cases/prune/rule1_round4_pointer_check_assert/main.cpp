// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct F {
 int operator()(long) const { return 0; }
 int operator()(unsigned long) const { return 0; }
};
struct Holder { F* p; };
template<class H> concept Accepted = requires(H h) { (*h.p)(0); };
static_assert(!Accepted<Holder>);
int main() { F f; std::printf("%d\n", f(0L) + Accepted<Holder>); }
