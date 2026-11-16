// SPDX-License-Identifier: GPL-3.0-or-later
#include <functional>
struct Key {};
namespace std { template<> struct hash<Key> { unsigned long operator()(Key) const { return 0; } }; }
int main() { Key key; (void)key; return 0; }
