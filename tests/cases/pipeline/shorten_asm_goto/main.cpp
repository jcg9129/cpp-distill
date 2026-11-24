// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#define GO_TO_LABEL(labelParameter) asm goto(""::::labelParameter);
int main(){int longLocal=9; GO_TO_LABEL(longLabel) longLabel: std::printf("%d\n",longLocal);}
