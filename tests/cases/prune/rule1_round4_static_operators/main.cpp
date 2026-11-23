// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct S {static int operator()(int x){return x+1;} static int operator[](int x){return x+2;}};
int main(){S s;std::printf("%d\n",s(1)+s[1]);}
