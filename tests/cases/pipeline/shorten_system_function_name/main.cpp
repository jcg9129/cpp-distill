// SPDX-License-Identifier: GPL-3.0-or-later
#include <utility>
extern "C" int puts(const char*);
struct LongUserType {};
int main() { puts(describe_type<LongUserType>()); }
