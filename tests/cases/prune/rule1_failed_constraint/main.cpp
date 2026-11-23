// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <type_traits>
struct ConstraintRecord {
  ConstraintRecord() = default;
  ConstraintRecord(int, int) {}
  int operator+(int) { return 81; }
};
template<class T> int select(T) requires (!__is_constructible(T,int,int)) { return 1; }
int select(...) { return 0; }
struct LibraryConstraintRecord {
  LibraryConstraintRecord() = default;
  LibraryConstraintRecord(int, int) {}
  int operator+(int) { return 82; }
};
template<class T> int select_library(T) requires (!std::is_constructible_v<T,int,int>) { return 1; }
int select_library(...) { return 0; }
int main() {
  ConstraintRecord r;
  LibraryConstraintRecord lib;
  std::printf("%d\n", select(r)+select_library(lib));
}
