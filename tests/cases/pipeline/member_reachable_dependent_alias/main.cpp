// SPDX-License-Identifier: GPL-3.0-or-later
#include <type_traits>
struct Result { int init() { return 4; } };
struct Carrier { using type = Result; };
template<class T> struct User {
  static void dormant() { std::type_identity<typename T::type> value; (void)value; }
};
int main() { User<Carrier> u; (void)u; }
