// SPDX-License-Identifier: GPL-3.0-or-later
#define DEFAULT_VALUE 1
struct R {};
R helper(R r, int) { return r; }
R helper(R, int = DEFAULT_VALUE);
int main() { R r; (void)r; }
