// SPDX-License-Identifier: GPL-3.0-or-later
#include <type_traits>
struct Owner { int init() { return 1; } };
struct Result { int find() { return 2; } };
struct Param { int push() { return 3; } };
using Shape = std::type_identity<Result (Owner::*)(const Param (&)[2])>;
int main() { Shape::type p = nullptr; return p != nullptr; }
