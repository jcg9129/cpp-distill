// SPDX-License-Identifier: GPL-3.0-or-later
#define ITEMS(X) X(one) X(two)
#define X(name) int name=1;
ITEMS(X)
#undef X
#define X(name) +name
int main(){return (0 ITEMS(X)) != 2;}
