// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#ifndef FORM
#define FORM 0
#endif
#ifndef DEDUCED
#define DEDUCED 0
#endif
#ifndef SEED
#define SEED 1
#endif
#if FORM == 0
#define PARAM C
#define ARG C<int>
#elif FORM == 1
#define PARAM C const&
#define ARG C<int> const&
#else
#define PARAM C*
#define ARG C<int>*
#endif
#if DEDUCED
#define RET auto
#else
#define RET int
#endif
template<class T> struct C { friend RET supplied(PARAM) { return 7; } };
RET supplied(ARG);
#if SEED
struct Dormant { decltype(sizeof(C<int>)) declaration(); };
#endif
auto *volatile address = &supplied;
int main() { std::puts(address ? "present" : "missing"); }
