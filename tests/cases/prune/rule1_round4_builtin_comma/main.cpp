// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct X {int operator,(int) const {return 9;}};
int main(){X x;std::printf("%d\n",(void(x),1));}
