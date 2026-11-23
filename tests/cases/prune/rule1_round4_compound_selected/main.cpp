// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct N {int value=0; N& operator+=(int n){value+=n;return *this;}
 N& operator++(){return *this += 1;}};
int main(){N n;++n;std::printf("%d\n",n.value);}
