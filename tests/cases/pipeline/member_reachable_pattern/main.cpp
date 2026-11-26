// SPDX-License-Identifier: GPL-3.0-or-later
#include <vector>
template<class T> struct Box { int init() { return 8; } };
template<> struct Box<char> { int init() { return 9; } };
int main() { std::vector<Box<int>> c(1); Box<long> other; Box<char> special; (void)other; (void)special; return c.empty(); }
