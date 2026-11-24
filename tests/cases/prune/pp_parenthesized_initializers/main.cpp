#include <cstddef>
auto lambda = []() {}
#include <cstddef> // lambda initializer
;
#include <cstddef> // completed declaration
template<class T> constexpr bool satisfied = requires(T t) { t(); }
#include <cstddef> // requires initializer
;
#include <cstddef> // completed declaration
[[gnu::aligned(8)]] int attributed{}
#include <cstddef> // attribute initializer
;
#include <cstddef> // completed declaration
int main() { lambda(); return attributed; }
