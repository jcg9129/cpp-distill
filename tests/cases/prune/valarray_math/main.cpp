// SPDX-License-Identifier: GPL-3.0-or-later
#include <valarray>
namespace app {
struct Number { int n = 0; };
Number abs(Number x){return {x.n<0 ? -x.n : x.n};}
Number sqrt(Number x){return {x.n==4 ? 2 : 0};}
}
int unused(){return 9;}
int main(){std::valarray<app::Number> x(app::Number{-4},2); std::valarray<app::Number> y=std::abs(x); std::valarray<app::Number> z=std::sqrt(y); return z[0].n==2 ? 0 : 1;}
