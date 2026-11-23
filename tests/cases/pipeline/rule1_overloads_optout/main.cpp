// SPDX-License-Identifier: GPL-3.0-or-later
struct Counter {
  int value=0;
  Counter() = default;
  Counter(int a,int b) : value(a+b) {}
  Counter(int,double) { value=71; }
  int operator+(int n) const { return value+n; }
  int operator+(double) const { return 72; }
};
int main() { Counter c(2,3); return c+2-7; }
