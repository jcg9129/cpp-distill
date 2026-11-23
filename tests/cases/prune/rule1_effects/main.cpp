// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
int calls = 0;
int observe() { ++calls; return 9; }
template<int N> struct Effect { inline static int value = observe(); };
struct Candidate {
  Candidate() = default;
  Candidate(int, double) { (void)Effect<0>::value; }
};
int main() {
  Candidate c;
  std::printf("%d\n", calls);
}
