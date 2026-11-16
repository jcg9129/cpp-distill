// SPDX-License-Identifier: GPL-3.0-or-later
#define DIRECTIVE(x) _Pragma(#x)
DIRECTIVE(distill_unknown symbol_hook)
int main() {}
