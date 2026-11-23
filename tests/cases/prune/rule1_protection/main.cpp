// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstddef>
#include <cstdio>
struct Base {
  Base() = default;
  Base(int, double) {}
};
struct Derived : Base { using Base::Base; };
template<class T> struct TemplateBase {
  TemplateBase() = default;
  TemplateBase(int, double) {}
};
template<class T> struct TemplateDerived : TemplateBase<T> {
  using TemplateBase<T>::TemplateBase;
};
struct Structural {
  Structural() = default;
  Structural(const Structural&) {}
  Structural(Structural&&) {}
  ~Structural() {}
  Structural& operator=(const Structural&) { return *this; }
  Structural& operator=(Structural&&) { return *this; }
  operator int() const { return 0; }
  virtual int virtual_member() { return 0; }
  static void* operator new(std::size_t);
  static void operator delete(void*);
  int operator co_await() const { return 1; }
  int operator+(int) const = delete;
  bool operator==(const Structural&) const = default;
};
struct Flagged {
  Flagged() = default;
  Flagged(int, int) {}
  int operator+(int) { return 0; }
};
void discarded() { Flagged x(1,2); (void)(x+1); }
int main() {
  Derived d;
  TemplateBase<int> base;
  TemplateDerived<double>* derived = nullptr;
  (void)base; (void)derived;
  Structural s;
  Flagged f;
  std::printf("%d\n", int(s)+0);
}
