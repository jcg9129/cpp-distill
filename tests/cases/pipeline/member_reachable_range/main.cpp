// SPDX-License-Identifier: GPL-3.0-or-later
#include <vector>
struct Range {
  int values[2] = {2, 3};
  int* begin() { return values; }
  int* end() { return values + 2; }
  int init() { return 0; }
};
int main() { Range c; int sum = 0; for (int n : c) sum += n; return sum != 5; }
