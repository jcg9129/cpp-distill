// SPDX-License-Identifier: GPL-3.0-or-later
#include <vector>
struct Helper {
  int push() { return 1; }
  int find() { return 2; }
  int init() { return 3; }
};
int main() { Helper c; (void)c; }
