// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
int unused(){
 return 9;
} int kept(){return __LINE__;}
int main(){std::printf("%d\n",kept()); return 0;}
