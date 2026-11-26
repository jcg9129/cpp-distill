// SPDX-License-Identifier: GPL-3.0-or-later
#include <vector>
struct Target {};
struct Source {
  operator Target() const { return {}; }
  int init() { return 4; }
};
int main() { std::vector<Target> values(1); Source s; (void)s; return values.empty(); }
