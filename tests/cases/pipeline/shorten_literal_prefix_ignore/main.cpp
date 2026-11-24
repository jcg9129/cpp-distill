// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#define IGNORE(...) 0
int main() {
  int quantity_count = IGNORE(Looooo"x");
  std::printf("%d\n", quantity_count);
  return quantity_count;
}
