// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#define STRINGIZE(x) #x
#define OPEN_STRINGIZE STRINGIZE(
const char* first  = OPEN_STRINGIZE a + b);
const char* second = OPEN_STRINGIZE a + b);
int main() { std::puts(first); std::puts(second); return first[1] != second[1]; }
