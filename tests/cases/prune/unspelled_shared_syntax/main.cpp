// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct ReturnRecord* return_record();
int parameter_record(struct ParameterRecord*);
enum ReturnEnum : int;
enum ReturnEnum return_enum();
struct ReturnRecord { int x; };
struct ParameterRecord { int x; };
int main() { ReturnRecord r{}; ParameterRecord p{}; ReturnEnum e{}; (void)r; (void)p; (void)e; std::puts("ok"); }
