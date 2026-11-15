// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
int a=__COUNTER__;
#include "a.h"
int main(){std::printf("%d %d %d\n",a,b,__COUNTER__);}
