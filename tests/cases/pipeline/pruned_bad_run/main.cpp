// SPDX-License-Identifier: GPL-3.0-or-later
#define CAT_I(a,b) a##b
#define CAT(a,b) CAT_I(a,b)
int chosen(int){return 7;}
template<class T> int call(T x){
 if constexpr(requires{CAT(cho,sen)(x);}) return CAT(cho,sen)(x);
 else return 0;
}
int unused(){return 9;}
int main(){return call(1);}
