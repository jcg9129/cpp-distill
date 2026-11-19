// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#if 0
#include "leaf.h"
#endif
#if 0
#include "left.h"
#elif 0
#include "leaf.h"
#else
#include "right.h"
#endif
#include "left.h"
#include "series.hpp"
#include "detail/series.hpp"
#include "./leaf.h"
int main() {
  std::printf("%s:%d %s:%d %s:%d %s:%d\n", leaf_file, leaf_line,
      detail_file, detail_line, outer_file, outer_line, __FILE__, __LINE__);
}
