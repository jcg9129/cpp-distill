// SPDX-License-Identifier: GPL-3.0-or-later
#include <tuple>
#include <cstdio>
struct LongPair { int get() const { return 7; } };
namespace std {
template<> struct tuple_size<LongPair> { static constexpr unsigned value = 1; };
template<> struct tuple_element<0,LongPair> { using type = int; };
}
template<unsigned> int get(LongPair const& x) { return x.get(); }
struct LongRange { int data[2] = {3,4}; int* begin(){return data;} int* end(){return data+2;} };
int main() { auto [element] = LongPair{}; LongRange range; int total=element; for(int x:range)total+=x; std::printf("%d\n",total); }
