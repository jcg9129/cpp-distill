// SPDX-License-Identifier: GPL-3.0-or-later
#define CAT_I(a,b) a##b
#define CAT(a,b) CAT_I(a,b)
int chosen(){return 7;}
int unused(){return 9;}
int main(){return CAT(cho,sen)();}
