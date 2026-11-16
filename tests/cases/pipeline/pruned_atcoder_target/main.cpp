// SPDX-License-Identifier: GPL-3.0-or-later
#pragma GCC target("avx2")
#if defined(__x86_64__)
int unused() { return 1; }
int main() { return 0; }
#else
#error AtCoder requires x86_64
#endif
