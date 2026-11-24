// SPDX-License-Identifier: GPL-3.0-or-later
#include <cxxabi.h>
#include <cstdio>
struct LongThrownType {};
int main() { try { throw LongThrownType{}; } catch (...) { std::puts(abi::__cxa_current_exception_type()->name()); } }
