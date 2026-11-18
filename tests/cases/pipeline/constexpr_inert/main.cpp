// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <type_traits>
template<class T> struct Unused {
  static constexpr bool x = !std::is_scalar_v<T>;
};
template<class T> struct Used {
  static constexpr bool x = !std::is_scalar_v<T>;
};
struct UnusedTag {};
inline constexpr UnusedTag unused_tag{};
struct UsedTag {};
inline constexpr UsedTag used_tag{};
struct Convert {
  constexpr operator int() const { return 7; }
  constexpr ~Convert() {
    if (!std::is_constant_evaluated()) std::puts("temporary");
  }
};
constexpr int converted = Convert{};
struct Reporter {
  constexpr Reporter() = default;
  ~Reporter() { std::puts("specialized"); }
};
template<class T> constexpr bool flag = true;
template<> Reporter flag<int>{};
int unrelated() { return 99; }
int main() {
  std::printf("%d %d\n", int(Used<int>::x), int(&used_tag != nullptr));
}
