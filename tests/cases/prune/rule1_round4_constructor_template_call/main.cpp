// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct F {int operator()(long) const {return 0;} int operator()(unsigned long) const {return 0;}};
struct C {
 int value;
 template<class T> requires requires(T t){t(0);} C(T):value(1){}
 C(...):value(0){}
};
int main(){std::printf("%d\n",F{}(0L)+C{F{}}.value);}
