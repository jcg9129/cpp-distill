// SPDX-License-Identifier: GPL-3.0-or-later
#include <bits/stdc++.h>
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#elif defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#endif
#include <stddef.h>
#include <stdarg.h>
#if __cplusplus >= 202002L
#include <concepts>
#include <ranges>
#include <span>
template <std::integral T> constexpr T twice(T value) { return value + value; }
static_assert(twice(21) == 42);
#endif
int main() {
  std::vector<int> values{3, 1, 2};
  std::sort(values.begin(), values.end());
#if __cplusplus >= 202002L
  std::span<int> view(values);
  auto even = view | std::views::filter([](int x) { return x % 2 == 0; });
  return *even.begin() == 2 ? 0 : 1;
#else
  return values.front() == 1 ? 0 : 1;
#endif
}
