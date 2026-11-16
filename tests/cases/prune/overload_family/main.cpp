// SPDX-License-Identifier: GPL-3.0-or-later
int chosen(int x){return x;}
double chosen(double x){return x+1;}
int unused(){return 9;}
int main(){return chosen(0);}
