// SPDX-License-Identifier: GPL-3.0-or-later
#include <concepts>
struct Helper { int init() { return 4; } };
static_assert(std::same_as<Helper, Helper>);
int main() { Helper c; (void)c; }
