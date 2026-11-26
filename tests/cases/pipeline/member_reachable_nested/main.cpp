// SPDX-License-Identifier: GPL-3.0-or-later
#include <vector>
struct Outer {
  struct Nested { int init() { return 4; } };
};
int main() { std::vector<Outer> c(1); Outer::Nested n; (void)n; return c.empty(); }
