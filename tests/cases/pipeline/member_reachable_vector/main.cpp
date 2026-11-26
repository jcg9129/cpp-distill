// SPDX-License-Identifier: GPL-3.0-or-later
#include <vector>
struct Helper {
  int push() { return 1; }
  int find() { return 2; }
  int init() { return 3; }
};
int main() { std::vector<Helper> c(1); return c.empty(); }
