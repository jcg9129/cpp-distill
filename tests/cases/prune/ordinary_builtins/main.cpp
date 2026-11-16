// SPDX-License-Identifier: GPL-3.0-or-later
struct Unused {};
int input() { return 7; }
int main() {
  unsigned x = input();
  if (__builtin_expect_with_probability(x != 0, 1, 0.9)) {
    return __builtin_popcount(x) + __builtin_popcountl(x) + __builtin_popcountll(x)
      + __builtin_clz(x) + __builtin_clzl(x) + __builtin_clzll(x)
      + __builtin_ctz(x) + __builtin_ctzl(x) + __builtin_ctzll(x)
      + __builtin_ffs(x) + __builtin_ffsl(x) + __builtin_ffsll(x)
      + __builtin_parity(x) + __builtin_parityl(x) + __builtin_parityll(x)
      + __builtin_bswap16(x) + __builtin_bswap32(x) + int(__builtin_bswap64(x));
  }
  if (__builtin_expect(x == 0, 0)) return 0;
  __builtin_unreachable();
}
