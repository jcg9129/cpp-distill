// SPDX-License-Identifier: GPL-3.0-or-later
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
static_assert(sizeof(void *) == 8);
static_assert(sizeof(uint32_t) == 4);
template <class T> constexpr T twice(T n) { return n + n; }
static_assert(twice(21) == 42);
int main() { return 0; }
