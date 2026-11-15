// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#if 0
#include "a.h"
#else /* comment
ends here */
int value=__LINE__;
#endif /* more
comment */
int main(){std::printf("%s:%d:%d\n",__FILE__,__LINE__,value);}
