// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Literal {
  Literal() {}
  constexpr Literal(int) {}
  constexpr Literal(int, int) {}
};
struct Defaulted {
  Defaulted() = default;
  Defaulted(int) {}
};
struct Deleted {
  Deleted() = delete;
  Deleted(int) {}
};
constexpr int f(Literal) { return 0; }
int main() {
  Literal x;
  Defaulted* a = nullptr;
  Deleted* b = nullptr;
  std::printf("%d\n", f(x) + __is_aggregate(Defaulted) + __is_aggregate(Deleted) + (a != nullptr) + (b != nullptr));
}
