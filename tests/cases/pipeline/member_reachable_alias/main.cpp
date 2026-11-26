// SPDX-License-Identifier: GPL-3.0-or-later
#include <type_traits>
struct Helper { int init() { return 4; } };
using Pointer = std::type_identity_t<Helper*>;
int main() { Pointer p = nullptr; return p != nullptr; }
