// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <type_traits>
int discarded_ctor_helper() { return 81; }
struct Sized {
  Sized() = default;
  explicit Sized(int) { (void)discarded_ctor_helper(); }
  Sized(int, int) {}
  int operator+(int) { return 82; }
};
template<class T> struct Storage {
  T value;
  static int size() { return sizeof(T); }
  bool same_layout() { return std::is_standard_layout_v<T>; }
};
template<class T> int layout(T&) { return sizeof(T) + alignof(T); }
static_assert(sizeof(Sized) > 0 && alignof(Sized) > 0);
static_assert(std::is_trivially_copyable_v<Sized>);
static_assert(std::is_standard_layout_v<Sized>);
static_assert(std::is_same_v<Sized,Sized> && std::is_class_v<Sized>);
static_assert(std::is_destructible_v<Sized>);
int main() {
  Storage<Sized> storage;
  Sized sized;
  std::printf("%d\n", int(layout(sized) > 0) + int(storage.size() > 0) + storage.same_layout());
}
