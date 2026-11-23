// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct S {int value; int operator[](int) const {return 99;}
 S* operator->(){return this;} int operator*() const {return 99;}};
int main(){auto [x]=S{7};std::printf("%d\n",x);}
