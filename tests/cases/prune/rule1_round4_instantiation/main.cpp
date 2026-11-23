// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
template<class T> struct F {int operator()(T) const {return 1;} int operator[](int) const {return 2;}};
int main(){std::printf("%d\n",F<int>{}(0)+F<double>{}[0]);}
