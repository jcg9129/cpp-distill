// SPDX-License-Identifier: GPL-3.0-or-later
template<class T> struct Live { template<class U> int f(U); };
template<class T> template<class U> int Live<T>::f(U value){return value;}
template<class T> struct Dead { template<class U> void f(U); };
template<class T> template<class U> void Dead<T>::f(U){}
template<class T> int forwarded();
int main(){Live<int> x; return x.f(3)-forwarded<int>();}
template<class T> int forwarded(){return 3;}
