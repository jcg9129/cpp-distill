// SPDX-License-Identifier: GPL-3.0-or-later
[[nodiscard]] int unused_result() { return 1; }
[[maybe_unused]] int unused_variable = 0;
[[nodiscard("reason")]] [[maybe_unused]] int multiple() { return 0; }
__attribute__((always_inline)) inline int unused_inline() { return 2; }
[[gnu::always_inline]] inline int unused_scoped() { return 3; }
alignas(16) int unused_aligned = 0;
struct alignas(32) UnusedAligned { int x; };
[[nodiscard]] int used_result() { return 3; }
[[nodiscard]] int repeated();
int between() { return 9; }
int repeated() { return 1; }
int main() { if (used_result() + repeated()) [[likely]] return 0; else [[unlikely]] return 1; }
