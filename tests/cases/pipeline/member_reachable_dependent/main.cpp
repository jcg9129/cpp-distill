// SPDX-License-Identifier: GPL-3.0-or-later
#include <vector>
struct Helper { int init() { return 4; } };
template<class T> struct User {
  // The dependent body is deliberately never instantiated.
  static int consume() { std::vector<T> values; return values.empty(); }
};
int main() { Helper c; User<Helper> u; (void)c; (void)u; }
