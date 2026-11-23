// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
template<class T> struct F {int operator()(long) const {return 0;} int operator()(unsigned long) const {return 0;}};
template<template<class> class C> struct Holder {
 template<class T> static auto call() -> decltype(C<T>{}(0));
};
template<class H> concept Accepted = requires { H::template call<int>(); };
static_assert(!Accepted<Holder<F>>);
int main(){std::printf("%d\n",F<int>{}(0L)+Accepted<Holder<F>>);}
