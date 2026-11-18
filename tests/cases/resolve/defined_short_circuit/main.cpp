// SPDX-License-Identifier: GPL-3.0-or-later
// Shape of atl-cpp include/atl/io/reader.hpp:42. In this approximate profile
// a standard include leaves defined(__has_include) unknown.
#include <cstdio>
#if defined(ATL_USE_MMAP) && defined(__has_include)
#if __has_include(<sys/mman.h>)
#define ATL_HAS_MMAP 1
#endif
#endif
#ifndef ATL_HAS_MMAP
#define ATL_HAS_MMAP 0
#endif
#if ATL_HAS_MMAP
int mmap_reader;
#endif
#if defined(__has_include) || 1
int always;
#endif
#if defined(__has_include)
int alone;
#endif
#if defined(ATL_USE_MMAP) && __has_include(<sys/mman.h>)
int value_use;
#endif
#if defined(__has_include) && !defined(ATL_USE_MMAP)
int depends;
#endif
int main(){return 0;}
