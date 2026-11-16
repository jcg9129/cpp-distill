// SPDX-License-Identifier: GPL-3.0-or-later
#define MODE 1
#include <vendor.h>
#if MODE
int value=1;
#else
int value=0;
#endif
int main(){return value;}
