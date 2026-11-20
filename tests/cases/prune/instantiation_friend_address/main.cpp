// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
namespace value {
template<class T> struct C { friend int supplied(C) { return 7; } };
int supplied(C<int>);
}
namespace reference {
template<class T> struct C { friend int supplied(C const&) { return 7; } };
int supplied(C<int> const&);
}
namespace pointer {
template<class T> struct C { friend int supplied(C*) { return 7; } };
int supplied(C<int>*);
}
struct Dormant {
  decltype(sizeof(value::C<int>)) value_trigger();
  decltype(sizeof(reference::C<int>)) reference_trigger();
  decltype(sizeof(pointer::C<int>)) pointer_trigger();
};
auto* volatile value_address = &value::supplied;
auto* volatile reference_address = &reference::supplied;
auto* volatile pointer_address = &pointer::supplied;
struct Dead {};
int main() { std::printf("%d\n", int(value_address && reference_address && pointer_address)); }
