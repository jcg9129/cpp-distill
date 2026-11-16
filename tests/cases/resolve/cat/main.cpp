// SPDX-License-Identifier: GPL-3.0-or-later
#define CAT(a,b) a ## b
#define chosen1 1
#if CAT(chosen,1)
int x;
#endif
int main(){return 0;}
