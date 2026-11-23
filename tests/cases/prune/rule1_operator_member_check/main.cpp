// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Member {
  int operator+(long) { return 1; }
  int operator+(unsigned long) { return 2; }
};
struct Outer { Member member; };
template<class T> concept Accepted = requires(T value) { value.member + 0; };
int main() {
  Outer outer;
  std::printf("%d %d\n", outer.member + 0L, int(Accepted<Outer>));
}
