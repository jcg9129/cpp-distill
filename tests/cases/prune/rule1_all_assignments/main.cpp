// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Assignments {
  Assignments() = default;
  Assignments(const Assignments&) = default;
  Assignments(Assignments&&) = default;
  Assignments& operator=(const Assignments&) = default;
  Assignments& operator=(Assignments&&) = default;
  Assignments& operator=(int) { return *this; }
  Assignments& operator=(long) = delete;
  template<class T> Assignments& operator=(T*) { return *this; }
  template<class T> Assignments& operator=(const T&) { return *this; }
  Assignments& operator+=(int) { return *this; }
  template<class T> Assignments& operator-=(T) { return *this; }
};
int main() { Assignments a; std::printf("0\n"); }
