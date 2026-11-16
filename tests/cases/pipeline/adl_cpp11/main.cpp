// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
struct Item {int value;};
void swap(Item& a,Item& b){int t=a.value;a.value=b.value;b.value=t;}
int unused(){return 7;}
int main(){Item a{1},b{2};std::iter_swap(&a,&b);return a.value!=2;}
