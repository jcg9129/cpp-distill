// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#if 1
#include "a.h"
#elif 1
#include "a.h"
#else
#include "a.h"
#endif
int main(){std::printf("%s:%d:%d\n",__FILE__,__LINE__,value);}
