// SPDX-License-Identifier: GPL-3.0-or-later
  #define TEXT(x) #x
  #define VALUE 1 +\
    +2
  static_assert(VALUE == 3);
  constexpr auto text = TEXT(alpha\
    beta);
  static_assert(sizeof("alpha beta") == sizeof(TEXT(alpha\
    beta)));
  static_assert(text[5] == ' ' && text[6] == 'b');

  int main(){return 0;}
