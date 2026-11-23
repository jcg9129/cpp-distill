// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <functional>
#include <set>
#include <cstdio>
struct Less { bool operator()(int a,int b) const {return a<b;} };
struct Inc { int operator()(int x) const {return x+1;} };
struct Odd { bool operator()(int x) const {return x%2!=0;} };
int main(){int a[]{2,1}; std::sort(a,a+2,Less{}); std::set<int,Less> s{2,1};
 std::function<int(int)> f=Inc{}; std::printf("%d\n",f(*s.begin())+std::ranges::count_if(a,Odd{}));}
