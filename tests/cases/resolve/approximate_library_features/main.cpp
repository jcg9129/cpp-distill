// SPDX-License-Identifier: GPL-3.0-or-later
#if defined(__cpp_lib_mdspan)
int before;
#endif
#include <version>
#if defined(__cpp_lib_mdspan)
int mdspan;
#endif
#ifdef __cpp_lib_ranges
int ranges;
#endif
#ifndef __cpp_lib_mdspan
int no_mdspan;
#endif
#if __cpp_lib_ranges >= 202110L
int newer_ranges;
#endif
#if defined(__cpp_lib_zzz_future)
int future;
#endif
#if __cpp_constexpr == 202211L
int compiler_feature;
#endif
#include <bits/stdc++.h>
#ifdef __cpp_lib_mdspan
int bits_mdspan;
#endif
#include <vector>
#if __cpp_lib_ranges >= 202110L
int vector_ranges;
#endif
#include "features.h"
#if defined(__cpp_lib_zzz_future)
int after_user_header;
#endif
