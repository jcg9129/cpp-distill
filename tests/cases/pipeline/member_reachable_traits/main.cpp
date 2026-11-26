// SPDX-License-Identifier: GPL-3.0-or-later
#include <string>
struct MyTraits : std::char_traits<char> {
  using std::char_traits<char>::assign;
  static const char* find(const char* p, std::size_t n, char c) {
    return std::char_traits<char>::find(p, n, c);
  }
  static void assign(char& a, const char& b) { a = b; }
  static char* copy(char* a, const char* b, std::size_t n) {
    return std::char_traits<char>::copy(a, b, n);
  }
};
int main() { std::basic_string<char, MyTraits> s("abc"); return s[1] != 'b'; }
