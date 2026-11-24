// SPDX-License-Identifier: GPL-3.0-or-later
#define REQUIREMENT(T) requires (sizeof(T) > 0)
struct R {};
template<class T>
R helper(T) REQUIREMENT(T) { return {}; }
int main() { R r; (void)r; }
