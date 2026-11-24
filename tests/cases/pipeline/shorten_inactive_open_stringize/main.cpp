// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#define STR(x) #x
#define OPEN_STRINGIZE STR(
#if 0
const char* text = OPEN_STRINGIZE long_identifier);
#endif
int long_identifier = 7;
int main() { std::printf("%d\n", long_identifier); }
