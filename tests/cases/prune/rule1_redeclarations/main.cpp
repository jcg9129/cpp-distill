// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Out {
  Out() = default;
  Out(int, double);
  int operator+(int) const;
  int operator+(double) const;
};
Out::Out(int, double) {}
int Out::operator+(int) const { return 3; }
int Out::operator+(double) const { return 81; }
template<class T> struct Template {
  Template() = default;
  Template(int, double);
  int operator+(int) const;
  int operator+(double) const;
};
template<class T> Template<T>::Template(int, double) {}
template<class T> int Template<T>::operator+(int) const { return 4; }
template<class T> int Template<T>::operator+(double) const { return 82; }
template<> int Template<int>::operator+(double) const { return 83; }
int main() {
  Out x;
  Template<int> y;
  std::printf("%d\n", x+1+(y+1));
}
