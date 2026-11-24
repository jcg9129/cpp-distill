// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#define STRINGIZE(x) #x
int STRINGIZE = 7;
int main() { std::printf("%d\n", STRINGIZE); }
