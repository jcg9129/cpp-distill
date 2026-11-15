// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace test {
struct Case { const char* name; void (*run)(); };
inline std::vector<Case>& cases() { static std::vector<Case> value; return value; }
inline int failures = 0;
struct Register { Register(const char* name, void (*run)()) { cases().push_back({name, run}); } };
struct Skip { std::string reason; };
template <class T> std::string show(const T& value) {
  std::ostringstream stream;
  if constexpr (requires { stream << value; }) stream << value;
  else if constexpr (std::is_enum_v<T>) stream << static_cast<std::underlying_type_t<T>>(value);
  else if constexpr (requires { value.begin(); value.end(); }) {
    stream << '[';
    for (const auto& item : value) stream << show(item) << ", ";
    stream << ']';
  } else stream << "<value>";
  return stream.str();
}
inline void check(bool condition, const char* expression, const char* file, int line) {
  if (!condition) { ++failures; std::cerr << file << ':' << line << ": CHECK(" << expression << ") failed (false)\n"; }
}
template <class A, class B> void equal(const A& a, const B& b, const char* expression, const char* file, int line) {
  if (!(a == b)) { ++failures; std::cerr << file << ':' << line << ": CHECK_EQ(" << expression << ") failed: " << show(a) << " != " << show(b) << '\n'; }
}
} // namespace test
#define TEST(name) static void name(); static test::Register register_##name(#name, name); static void name()
#define CHECK(cond) test::check(static_cast<bool>(cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a,b) test::equal((a), (b), #a ", " #b, __FILE__, __LINE__)
