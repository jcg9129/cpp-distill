// SPDX-License-Identifier: GPL-3.0-or-later
#define NOTHROW true
struct R {};
R helper(R r) noexcept(NOTHROW) { return r; }
int main() { R r; (void)r; }
