// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
namespace LongNamespace {
struct LongType {int longField;};
using LongAlias=LongType;
int longTarget(LongAlias object){return object.longField;}
}
int longAlias(LongNamespace::LongType) __attribute__((alias("_ZN13LongNamespace10longTargetENS_8LongTypeE")));
int main(){int longLocal=9;std::printf("%d\n",longAlias({longLocal}));}
