// SPDX-License-Identifier: GPL-3.0-or-later
#include <vector>
#include <iterator>
#include <stack>
struct Container {
  using value_type = int;
  using reference = int&;
  using const_reference = const int&;
  using size_type = unsigned long;
  std::vector<int> values;
  void push_back(int n) { values.push_back(n); }
  int& back() { return values.back(); }
  const int& back() const { return values.back(); }
  bool empty() const { return values.empty(); }
  void pop_back() { values.pop_back(); }
  void insert(int n) { values.push_back(n); }
};
int main() {
  Container c;
  auto out = std::back_inserter(c);
  *out = 7;
  std::stack<int, Container> s;
  s.push(3);
  return s.top() != 3 || c.values[0] != 7;
}
