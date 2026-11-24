// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#define PP_NUMBER_EDGE 1.e+ 1
template<class LongType> struct LongTemplate{LongType item;};
struct GlobalType{};
int main(){int alpha=4,beta=2; int* pointer=&alpha;
 double exponent=1.e+1;
 LongTemplate<::GlobalType> object;
 LongTemplate<LongTemplate<int>> nested{{7}};
 const auto* utf8=u8"hello";
 const char* raw=R"tag(raw   text
next line)tag";
 std::printf("%d %.0f %d %c %c\n",alpha+ +beta+alpha- -beta+alpha/ *pointer,exponent,nested.item.item,char(utf8[0]),raw[0]);
 (void)object;
}
