// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <default_location.h>
#if 0
#include "dead.h"
#else
int value=call_line();
#endif
#include "live.h"
int main(){std::printf("%s:%d:%d:%d:%s\n",__FILE__,__LINE__,value,call_line(),call_file());}
