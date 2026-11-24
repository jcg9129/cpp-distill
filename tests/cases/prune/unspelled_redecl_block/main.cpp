// SPDX-License-Identifier: GPL-3.0-or-later
struct R {};
R helper();
void dead() { extern R helper(); }
int main() { R r; (void)r; }
