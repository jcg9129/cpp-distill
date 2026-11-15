// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#define PRINT std::printf("%s:%d:%d\n", __FILE__, __LINE__, __builtin_LINE());
#if 0
#include "dead.h"
PRINT
#elif 0
#include "dead.h"
#else
int first(){ PRINT return 1; }
#if 0
#include "dead.h"
#else
int second(){ PRINT return 2; }
#endif
#endif
int between(){ PRINT return 3; }
#if 1
int third(){ PRINT return 4; }
#else
#include "dead.h"
#endif
#if 0
#if 1
#include "dead.h"
#else
#include "dead.h"
#endif
#else
int fourth(){ PRINT return 5; }
#endif
#include "live.h"
int main(){ PRINT return first()+second()+between()+third()+fourth()+live()!=21; }
