// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Target { Target(long) {} Target(unsigned long) {} };
void accept(Target);
template<class T> concept Accepted = requires(T value) { accept(value); };
int main() {
  Target keep(0L);
  std::printf("%d\n", int(Accepted<int>));
  return Accepted<int>;
}
