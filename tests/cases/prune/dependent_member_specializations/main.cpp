// SPDX-License-Identifier: GPL-3.0-or-later
template<int N> struct Box {
  int get() const;
  template<class U> int f(U) const;
  static int value;
};
template<int N> int Box<N>::get() const { return N; }
template<int N> template<class U> int Box<N>::f(U x) const { return int(x); }
template<int N> int Box<N>::value = N;
template<> int Box<4>::get() const { return 4; }
template<> int Box<8>::get() const { return 8; }
template<> template<> int Box<4>::f<int>(int x) const { return 4+x; }
template<> template<> int Box<8>::f<int>(int x) const { return 8+x; }
template<> int Box<4>::value = 4;
template<> int Box<8>::value = 8;
template<class T> int run(T x) { return x.get() + x.template f<int>(1) + T::value; }
struct Unused { int get() const { return 0; } };
int main() { return run(Box<sizeof(long)>{}); }
