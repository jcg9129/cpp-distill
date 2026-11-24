// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#define STRINGIZE(x) #x
#define ALIAS STRINGIZE
const char* first = ALIAS(a + b);
const char* second = ALIAS(a + b);
int main() { std::puts(first); std::puts(second); return first[1] != second[1]; }
