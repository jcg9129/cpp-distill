// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#define STR(x) #x
#define FORWARD(x) STR(x)
#define OPEN_STRINGIZE STR(
#define CAT(a, b) a##b
const char* a = FORWARD(long_identifier);
const char* b = OPEN_STRINGIZE other_identifier);
int long_result = 7;
int main() {
  int quantity_count = CAT(long_, result);
  std::printf("%s %s %d\n", a, b, quantity_count);
  return quantity_count != 7;
}
