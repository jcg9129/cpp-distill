// SPDX-License-Identifier: GPL-3.0-or-later
#define NDEBUG
#include <cassert>
#ifdef assert
int value=1;
#else
int value=2;
#endif
int main(){assert(false);return value!=1;}
