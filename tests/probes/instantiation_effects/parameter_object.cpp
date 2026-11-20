// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <type_traits>
int counter;
struct Value {
#ifdef NON_CONSTANT_DESTRUCTION
  ~Value() {}
#else
  constexpr ~Value() {
#ifdef BAD_CONSTANT_DESTRUCTION
    if (std::is_constant_evaluated()) ++counter;
#else
    if (!std::is_constant_evaluated()) std::puts("destroy");
#endif
  }
#endif
};
template<Value V> struct Object { static constexpr auto address = &V; };
auto pointer = Object<Value{}>::address;
int main() { std::puts(pointer ? "main" : "missing"); }
