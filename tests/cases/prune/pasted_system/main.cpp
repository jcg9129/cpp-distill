// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#define JOIN_I(a,b) a##b
#define JOIN(a,b) JOIN_I(a,b)
int JOIN(pick, BUFSIZ)() { return 1; }
int main() { return 0; }
