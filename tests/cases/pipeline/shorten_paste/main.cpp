// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#define PASTE(leftSpelling,rightSpelling) leftSpelling ## rightSpelling
#define FORWARD_PASTE(leftSpelling,rightSpelling) PASTE(leftSpelling,rightSpelling)
#define OBJECT_PREFIX longGenerated
int main(){int longGeneratedValue=6; int longPlainLocal=4;
 std::printf("%d\n",FORWARD_PASTE(OBJECT_PREFIX,Value)+longPlainLocal);}
