// SPDX-License-Identifier: GPL-3.0-or-later
struct R {};
R helper();
struct Unused { friend R helper(); };
int main() { R r; (void)r; }
