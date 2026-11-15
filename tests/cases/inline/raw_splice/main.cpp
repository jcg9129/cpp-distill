// SPDX-License-Identifier: GPL-3.0-or-later
const char* text=R"x(body )x\
"
#include "missing.h"
)x";
int main(){return text[0]!='b';}
