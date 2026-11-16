// SPDX-License-Identifier: GPL-3.0-or-later
template<class T> struct Box { T value; };
template struct Box<int>;
extern template struct Box<long>;
int main() { return 0; }
