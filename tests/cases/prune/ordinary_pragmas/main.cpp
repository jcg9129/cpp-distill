// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#pragma GCC push_options
#pragma GCC optimize("O3")
#pragma GCC target("avx2")
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma pack(push, 1)
struct Packed { char c; int n; };
struct Unused { int n; };
#pragma pack(pop)
#pragma message("pruning fixture")
#pragma warning(disable: 4996)
#pragma GCC diagnostic pop
#pragma GCC pop_options
#pragma GCC reset_options
void pinned() {
#pragma GCC diagnostic push
#pragma GCC diagnostic pop
}
int main() { return sizeof(Packed); }
