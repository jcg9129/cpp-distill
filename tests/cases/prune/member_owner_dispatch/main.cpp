// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <vector>
int registrations = 0;
int register_value() { return ++registrations; }
template<class T> struct Value {
  static inline int state = register_value();
  int value;
  Value() : value(state) {}
  template<class U> Value(U n) : value(n + state) {}
  ~Value() {}
  operator int() const { return value; }
  int operator()() const { return value; }
  int discarded_ordinary_member() const { return 99; }
};
int main() {
  std::vector<Value<int>> values(2);
  values.emplace_back(5);
  std::printf("%d %d %d\n", registrations, int(values[0]), values[2]());
}
