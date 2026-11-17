// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <type_traits>
struct Empty {};
struct Value { int n = 7; };
struct Record {
  [[no_unique_address]] Empty empty;
  [[no_unique_address]] Value value;
};
struct UnusedRecord { [[no_unique_address]] Empty empty; int tail; };
int unrelated() { return 99; }
int compact_path() { return 11; }
int expanded_path() { return 23; }
template<class T> int layout_path() {
  if constexpr (sizeof(T) == sizeof(Value)) return compact_path();
  else return expanded_path();
}
int main() {
  Record record;
  std::printf("%zu %d %d %d\n", sizeof(Record),
              int(std::is_standard_layout_v<Record>), record.value.n,
              layout_path<Record>());
}
