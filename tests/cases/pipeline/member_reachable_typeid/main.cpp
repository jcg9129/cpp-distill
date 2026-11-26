// SPDX-License-Identifier: GPL-3.0-or-later
#include <typeinfo>
struct Helper { int init() { return 4; } };
int main() { const auto& info = typeid(Helper*); (void)info; }
