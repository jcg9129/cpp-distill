// SPDX-License-Identifier: GPL-3.0-or-later
#include <typeinfo>
#include <cstdio>
struct LongType {int longMember;};
int main(){LongType longObject{5}; std::printf("%s %d\n",typeid(longObject).name(),longObject.longMember);}
