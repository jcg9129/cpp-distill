// SPDX-License-Identifier: GPL-3.0-or-later
template<int N> struct Key { friend int probe(Key); };
template<int N> struct Source {
  using type = int;
  friend int probe(Key<N>) { return 7; }
};
template<int N> struct Carrier {
  void dormant(typename Source<N>::type);
};
void trigger() { Carrier<0> c; }

template<class T> int observe() {
  if constexpr (sizeof(long) == 4) return probe(Key<0>{});
  return 0;
}
int main() { return observe<void>(); }
