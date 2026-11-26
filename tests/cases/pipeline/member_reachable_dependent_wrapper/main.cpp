// SPDX-License-Identifier: GPL-3.0-or-later
#include <type_traits>
template<class T> struct Box { int init() { return 4; } };
template<class T> struct User {
  static void dormant() { std::type_identity<Box<T>> value; (void)value; }
};
int main() { User<int> u; Box<long> b; (void)u; (void)b; }
