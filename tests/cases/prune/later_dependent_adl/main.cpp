// SPDX-License-Identifier: GPL-3.0-or-later
#define JOIN_I(a,b) a##b
#define JOIN(a,b) JOIN_I(a,b)
namespace app { struct X { int payload[2]; }; }
template<class T> int run(T x) {
  if constexpr (requires { JOIN(custo,mize)(x); }) return JOIN(custo,mize)(x);
  else return 0;
}
namespace app {
template<class T> requires (sizeof(long) == 4 || sizeof(T) == 1)
int customize(T) { return 1; }
}
int main() { return run(app::X{}); }
