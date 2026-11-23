// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct X {friend int operator+(X,int n){return n+1;} int operator[](int){return 99;}};
int main(){std::printf("%d\n",X{}+1);}
