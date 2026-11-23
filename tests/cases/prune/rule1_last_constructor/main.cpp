// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Last {
  Last(int, int) {}
  Last(int, double) {}
  struct Nested {
    Nested(char, char) {}
    Nested(char, double) {}
  };
};
int main() {
  Last* p = nullptr;
  Last::Nested* nested = nullptr;
  struct Local { Local(int, int) {} };
  Local* local = nullptr;
  auto unused = [](int n) { return n+81; };
  (void)nested; (void)local; (void)unused;
  std::printf("%d\n", p == nullptr);
}
