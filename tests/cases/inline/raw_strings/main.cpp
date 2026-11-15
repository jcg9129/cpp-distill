// SPDX-License-Identifier: GPL-3.0-or-later
const char* a=R"tag(
#include "x.h"
#line 99 "fake"
# 98 "also-fake" 1 3
)tag";
const char8_t* b=u8R"(
#include "x.h"
)";
const wchar_t* c=LR"x(
#include "x.h"
)x";
const char16_t* d=uR"(
#include "x.h"
)";
const char32_t* e=UR"(
#include "x.h"
)";
#define MACRO_RAW R"(first
#include "missing.h"
last)" " suffix"
const char* macro_raw=MACRO_RAW;
int main(){return a[0]!='\n' || macro_raw[0]!='f';}
