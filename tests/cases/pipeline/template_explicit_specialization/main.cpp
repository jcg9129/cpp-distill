// SPDX-License-Identifier: GPL-3.0-or-later
template<class T> int chosen(){return 1;}
template<> int chosen<int>(){return 3;}
template<class T> struct Box { static int get(){return 1;} };
template<> struct Box<int> { static int get(){return 3;} };
template<class T> int deadSpecial(){return 1;}
template<> int deadSpecial<int>(){return 2;}
template<class T> struct DeadBox {};
template<> struct DeadBox<int> {};
int main(){return chosen<int>()-Box<int>::get();}
