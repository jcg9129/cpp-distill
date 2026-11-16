// SPDX-License-Identifier: GPL-3.0-or-later
#define V __GNUC__
#if V > 0
int selected;
#else
#error wrong
#endif
int main(){return 0;}
