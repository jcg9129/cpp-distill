// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct I {int* p;
 int& operator*() const {return *p;}
 I& operator++(){++p;return *this;}
 I operator++(int){I old=*this;++*this;return old;}
 bool operator!=(const I& o) const {return p!=o.p;}
};
struct R {int a[1]{7}; I begin(){return {a};} I end(){return {a+1};}};
int main(){R r;int result=0;for(int x:r)result+=x;std::printf("%d\n",result);}
