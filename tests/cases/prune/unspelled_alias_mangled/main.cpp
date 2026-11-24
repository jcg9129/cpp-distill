// SPDX-License-Identifier: GPL-3.0-or-later
// ELF/Itanium only. The alias roots its mangled target; pruning stays enabled.
struct R { int value; };
R target() { return {42}; }
R proxy() __attribute__((alias("_Z6targetv")));
int main() { return proxy().value != 42; }
