// SPDX-License-Identifier: GPL-3.0-or-later
#include <functional>
#include <utility>
template<class... T> int sum(T... xs) { if constexpr (sizeof...(T) > 0) return (xs + ...); else return 0; }
struct Unused {};
int main() {
  int n = 4;
  auto capture = [n, &ref=n](int x) { return ref + n + x; };
  auto recursive = [](auto self, int x) -> int { return x ? x * self(self, x-1) : 1; };
  std::function<int(int)> fn = capture;
  auto [a, b] = std::pair<int,int>{2, 3};
  return fn(sum(a, b)) + recursive(recursive, n);
}
