// SPDX-License-Identifier: GPL-3.0-or-later
extern "C" int puts(const char*);
const char* descriptive_function() { return __builtin_FUNCTION(); }
int main() { puts(descriptive_function()); }
