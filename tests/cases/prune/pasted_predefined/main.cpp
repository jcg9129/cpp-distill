// SPDX-License-Identifier: GPL-3.0-or-later
#define CAT_I(a,b) a##b
#define CAT(a,b) CAT_I(a,b)
int value = CAT(__GNUC, __);
int main() { return 0; }
