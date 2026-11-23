// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
template<class T> struct Specialized {
  int value = 0;
  Specialized() = default;
  Specialized(int, double);
  int operator+(int) const { return 81; }
  int operator-(int) const { return 82; }
};
template<class T> Specialized<T>::Specialized(int n, double) : value(n) {}
template<> Specialized<int>::Specialized(int n, double) : value(n+1) {}
template<> int Specialized<int>::operator+(int) const = delete;
int main() {
  Specialized<int> x(2,3.0);
  Specialized<double> y;
  std::printf("%d\n", x.value+y.value);
}
