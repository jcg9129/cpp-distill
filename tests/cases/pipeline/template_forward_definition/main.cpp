// SPDX-License-Identifier: GPL-3.0-or-later
template<class T> int forwarded();
template<class T> int deadForward();
int main(){return forwarded<int>()-3;}
template<class T> int forwarded(){return 3;}
template<class T> int deadForward(){return 7;}
