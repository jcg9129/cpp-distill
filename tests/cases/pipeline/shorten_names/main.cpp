// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
namespace LongNamespace {
template<class LongType> concept LongConcept = requires(LongType object) { object+object; };
template<LongConcept LongType> struct LongRecord {
  LongType longMember;
  LongType longMethod(LongType longParameter) { auto longLocal=longMember+longParameter; return longLocal; }
};
}
int main(){ LongNamespace::LongRecord<int> longInstance{.longMember=7};
 std::printf("%d\n",longInstance.longMethod(5)); }
