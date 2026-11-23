// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
int live_helper() { return 7; }
int discarded_helper() { return 81; }
struct DiscardedFunctor { int operator()() { return discarded_helper(); } };
struct Live {
  int discarded() {
    auto lambda = [](auto f) { return f(); };
    return lambda(DiscardedFunctor{});
  }
};
int main() {
  Live live;
  auto lambda = [] { return live_helper(); };
  std::printf("%d\n", lambda());
}
