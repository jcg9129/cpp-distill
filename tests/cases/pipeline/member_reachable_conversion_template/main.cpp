// SPDX-License-Identifier: GPL-3.0-or-later
#include <vector>
template<class T> struct Target {};
struct Source {
  template<class T> operator Target<T>() const { return {}; }
  int init() { return 4; }
};
int main() { std::vector<Target<int>> v(1); Source s; (void)s; return v.empty(); }
