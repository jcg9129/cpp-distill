// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <memory>
struct X { X* operator&(){return nullptr;} };
int main(){X x;std::printf("%d\n",int(std::addressof(x)!=nullptr));}
