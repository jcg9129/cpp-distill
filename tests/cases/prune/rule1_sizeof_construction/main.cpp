// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
struct Tested {
  Tested() = default;
  Tested(int, int) {}
};
template<class T> int checked(T*) {
  if constexpr (sizeof(long) == 4) return int(sizeof(T(1,2)));
  return 0;
}
template<class T> int captured(T* pointer) {
  auto lambda = [pointer] {
    if constexpr (sizeof(long) == 4) return int(sizeof(T(1,2)));
    return 0;
  };
  return lambda();
}
struct ReturnTested {
  ReturnTested() = default;
  ReturnTested(int, int) {}
};
template<class T> auto make_tested() -> decltype(T(1,2));
template<class T> int return_checked(T*) {
  if constexpr (sizeof(long) == 4) return int(sizeof(decltype(make_tested<T>())));
  return 0;
}
struct ArgumentTested {
  ArgumentTested() = default;
  ArgumentTested(int) {}
};
template<class T> int accept(T);
template<class T> int argument_checked(T*) {
  if constexpr (sizeof(long) == 4) return int(sizeof(accept<T>(1)));
  return 0;
}
int main() { std::printf("%d\n", checked(static_cast<Tested*>(nullptr)) + captured(static_cast<Tested*>(nullptr)) + return_checked(static_cast<ReturnTested*>(nullptr)) + argument_checked(static_cast<ArgumentTested*>(nullptr))); }
