// SPDX-License-Identifier: GPL-3.0-or-later
struct R {};
int payload = 7;
R helper();
R helper();
R helper() { (void)payload; return {}; }
int main() { R r; (void)r; }
