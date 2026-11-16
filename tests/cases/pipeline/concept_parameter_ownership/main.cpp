// SPDX-License-Identifier: GPL-3.0-or-later
template<class T> concept Add = requires(T a,T b){a+b;};
template<class T> concept Lambda = requires { [](T parameter){return parameter;}; };
template<Add T> int sum(T a,T b){return a+b;}
int unused(){return 7;}
int main(){static_assert(Lambda<int>);return sum(1,2)!=3;}
