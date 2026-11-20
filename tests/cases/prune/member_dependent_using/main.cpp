// SPDX-License-Identifier: GPL-3.0-or-later
#define JOIN(a,b) a##b
struct Base { void hidden_hook() {} };
template<class T> struct Derived : T {
  using T::JOIN(hidden_,hook);
};
template<class T> int selected() {
  if constexpr (sizeof(long) == 4) return sizeof(Derived<T>);
  return 0;
}
int main() { return selected<Base>(); }
