// SPDX-License-Identifier: GPL-3.0-or-later
#define JOIN(a,b) a ## b
#if JOIN(pick,__GNUC__)
int x;
#endif
int main(){return 0;}
