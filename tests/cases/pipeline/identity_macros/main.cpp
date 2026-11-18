// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
#include <intrin.h>
#endif
int unrelated(){return 99;}
int main(){std::uint64_t value=7; return value==7 ? 0 : 1;}
