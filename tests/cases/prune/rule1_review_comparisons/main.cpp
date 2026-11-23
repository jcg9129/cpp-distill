// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Comparisons {
  bool operator==(int) const { return false; }
  bool operator!=(int) const { return false; }
  int operator<=>(int) const { return 0; }
  bool operator<(int) const { return false; }
  bool operator>(int) const { return false; }
  bool operator<=(int) const { return false; }
  bool operator>=(int) const { return false; }
  int operator()() const { return 1; }
};
struct Dead { bool operator==(int) const { return false; } };
int main() { Comparisons c; (void)c; std::puts("0"); }
