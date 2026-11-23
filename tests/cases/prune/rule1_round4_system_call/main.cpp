// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <algorithm>
struct F {
 int operator()(long) const {return 0;}
 int operator()(unsigned long) const {return 0;}
};
struct Holder { F* get(); };
int main(){ F f; std::printf("%d\n", f(0L) + std::call(Holder{})); }
