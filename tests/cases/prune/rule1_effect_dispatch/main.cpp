// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
int calls = 0;
int observe() { ++calls; return 7; }
int discarded() { return 81; }
template<int N> struct Effect { static inline int value = observe(); };
// A library-spelled record name must not turn a constructor into a new
// unbounded hook when its forward-retention blocker is relaxed.
struct get {
  get() = default;
  get(int, double) { (void)Effect<0>::value; }
};
int main() {
  get value;
  std::printf("%d\n", calls);
}
