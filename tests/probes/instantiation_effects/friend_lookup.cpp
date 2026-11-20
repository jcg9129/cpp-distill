// SPDX-License-Identifier: GPL-3.0-or-later
// ADL forces instantiation even with pointer/reference arguments; a mere
// declaration, and a parenthesized call that suppresses ADL, need not.
template<class T> struct Incomplete;
template<class T> concept IncompletePointer = requires(T* p) { supplied(p); };
template<class T> concept IncompleteReference = requires(T* p) { supplied(*p); };
static_assert(!IncompletePointer<Incomplete<int>>);
static_assert(!IncompleteReference<Incomplete<int>>);
template<class T> struct Incomplete {
  friend int supplied(Incomplete*) { return 1; }
  friend int supplied(Incomplete const&) { return 2; }
};
static_assert(IncompletePointer<Incomplete<long>>);
static_assert(IncompleteReference<Incomplete<long>>);

int main() {}
