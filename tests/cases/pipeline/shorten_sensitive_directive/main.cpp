// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#define STRINGIZE(x) #x
const char* first = STRINGIZE(a + b);
#define OTHER 1
const char* second = STRINGIZE(a + b);
int main() { std::puts(first); std::puts(second); return first[1] != second[1]; }
