// SPDX-License-Identifier: GPL-3.0-or-later
#include <compare>
namespace app {
struct Value { int n; };
std::strong_ordering strong_order(Value a, Value b){return a.n <=> b.n;}
std::weak_ordering weak_order(Value a, Value b){return a.n <=> b.n;}
std::partial_ordering partial_order(Value a, Value b){return a.n <=> b.n;}
}
int unused(){return 9;}
int main(){app::Value a{1}, b{2}; return std::strong_order(a,b)<0 && std::weak_order(a,b)<0 && std::partial_order(a,b)<0 ? 0 : 1;}
