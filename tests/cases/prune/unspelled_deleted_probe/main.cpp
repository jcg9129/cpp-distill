// SPDX-License-Identifier: GPL-3.0-or-later
struct R {};
int pick(R) = delete;
int pick(...) { return 1; }
template<class T>
constexpr bool callable = requires(T t) { pick(t); };
static_assert(!callable<R>);
int main() {}
