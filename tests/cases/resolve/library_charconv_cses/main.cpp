// SPDX-License-Identifier: GPL-3.0-or-later
#include <charconv>
#if defined(__cpp_lib_to_chars)
int supported;
#endif
#include <thread>
#ifdef __cpp_lib_jthread
int threads;
#endif
