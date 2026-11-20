// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#if DTOR == 1
#include <type_traits>
#endif

struct Noisy {
  int value;
  constexpr Noisy() : value(42) {}
#if DTOR == 2
  constexpr ~Noisy() { if !consteval { std::puts("destroy"); } }
#elif DTOR == 1
  constexpr ~Noisy() {
    if (!std::is_constant_evaluated()) std::puts("destroy");
  }
#else
  ~Noisy() { std::puts("destroy"); }
#endif
};

#if TEMPLATE
template<class T> const T* local() {
#else
using T = Noisy;
const T* local() {
#endif
#if CONSTINIT
  constinit
#endif
#if THREAD
  thread_local T const t;
#else
  static T const t;
#endif
  return &t;
}

// Instantiate only through this externally visible, never-called function.
// noinline keeps the registration site identifiable even at -O2.
__attribute__((noinline)) const Noisy* dormant() {
#if TEMPLATE
  return local<Noisy>();
#else
  return local();
#endif
}

int main() {
  std::puts("main");
#if REACH
  dormant();
  dormant(); // registration must happen exactly once
#endif
}
