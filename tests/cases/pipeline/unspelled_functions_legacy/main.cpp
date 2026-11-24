// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Kept { int value; };
enum class KeptEnum { value };
Kept returning() { return {1}; }
int taking(Kept) { return 2; }
template<class T> Kept templated(T) { return {3}; }
int overloaded(Kept) { return 4; }
int overloaded(Kept, int) { return 5; }
int enum_helper(KeptEnum) { return 6; }
int main() { Kept r{0}; KeptEnum e{}; (void)r; (void)e; std::puts("ok"); }
