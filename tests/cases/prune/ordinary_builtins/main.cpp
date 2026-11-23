// SPDX-License-Identifier: GPL-3.0-or-later
struct Unused {};
int input() { return 7; }
int offset() { return 0; }
template<class T> constexpr void prefetch(T* p) {
  if !consteval {
    __builtin_prefetch(p);
    __builtin_prefetch(p, 0);
    __builtin_prefetch(p + offset(), 1, 3);
  }
}
static_assert((prefetch(static_cast<int*>(nullptr)), true));
int main() {
  unsigned x = input();
  prefetch(&x);
  if (__builtin_expect_with_probability(x != 0, 1, 0.9)) {
    auto result = __builtin_popcount(x) + __builtin_popcountl(x) + __builtin_popcountll(x)
      + __builtin_clz(x) + __builtin_clzl(x) + __builtin_clzll(x)
      + __builtin_ctz(x) + __builtin_ctzl(x) + __builtin_ctzll(x)
      + __builtin_ffs(x) + __builtin_ffsl(x) + __builtin_ffsll(x)
      + __builtin_parity(x) + __builtin_parityl(x) + __builtin_parityll(x)
      + __builtin_bswap16(x) + __builtin_bswap32(x) + int(__builtin_bswap64(x));
    return result == 0;
  }
  if (__builtin_expect(x == 0, 0)) return 0;
  __builtin_unreachable();
}
