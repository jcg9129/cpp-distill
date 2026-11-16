// SPDX-License-Identifier: GPL-3.0-or-later
#include <format>
struct Value { int n; };
namespace std { template<> struct formatter<Value> : formatter<int> {}; }
int main(){return 0;}
