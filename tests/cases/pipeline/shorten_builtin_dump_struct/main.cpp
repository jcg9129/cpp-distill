// SPDX-License-Identifier: GPL-3.0-or-later
extern "C" int printf(const char*, ...);
extern "C" int getchar();
struct S { int quantity_count; };
int main() {
  S s{7};
  if (getchar() == '!')
    __builtin_dump_struct(&s, printf);
}
