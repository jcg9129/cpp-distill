// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct type_info { int name() const {return 7;} };
struct type_index {};
struct any { int type() {return 7;} };
int target_type(){return 0;}
int main(){type_info longInfo;any longObject;std::printf("%d\n",longInfo.name()+longObject.type()+target_type());}
