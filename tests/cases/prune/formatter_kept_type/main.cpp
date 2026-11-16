// SPDX-License-Identifier: GPL-3.0-or-later
#include <format>
struct Value { int n; };
namespace std { template<> struct formatter<Value> : formatter<int> {
 auto format(Value v, format_context& ctx) const { return formatter<int>::format(v.n,ctx); }
}; }
int main(){Value value{7}; (void)value; return 0;}
