// SPDX-License-Identifier: GPL-3.0-or-later
#define TEXT(x) #x
  constexpr auto text = TEXT(alpha

    beta);
  static_assert(text[5] == ' ' && text[6] == 'b');
  constexpr auto raw = u8R"x(
    raw text   

)x";
  static_assert(raw[1] == ' ' && raw[16] == '\n');
  int unused(){return 1;}
  // distill: keep
  int pinned(){return 2;}
  int main(){return 0;}

