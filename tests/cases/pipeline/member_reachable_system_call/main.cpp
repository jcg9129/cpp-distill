// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstring>
struct Helper { int value = 0; int init() { return 4; } };
int main() { Helper c; return std::memcmp(&c, &c, sizeof c); }
