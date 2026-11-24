// SPDX-License-Identifier: GPL-3.0-or-later
#define RESULT R
struct R {};
auto helper(R r) -> RESULT { return r; }
int main() { R r; (void)r; }
