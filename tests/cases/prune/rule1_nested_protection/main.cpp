// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Outer {
  Outer() = default;
  Outer(int, double) {}
  struct Checked {
    Checked() = default;
    Checked(int, int) {}
  };
  struct Base {
    Base() = default;
    Base(char, double) {}
  };
  struct Sibling {
    Sibling() = default;
    Sibling(char, double) {}
  };
};
struct Derived : Outer::Base { using Outer::Base::Base; };
static_assert(__is_constructible(Outer::Checked,int,int));
int main() {
  Outer outer;
  Outer::Sibling sibling;
  Derived derived;
  std::printf("%d\n", int(sizeof(outer) > 0));
}
