// SPDX-License-Identifier: GPL-3.0-or-later
#include <format>
struct Item { int value; };
template<> struct std::formatter<Item> {
  constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }
  auto format(const Item& v, std::format_context& ctx) const {
    return std::format_to(ctx.out(), "{}", v.value);
  }
};
int main() { return std::format("{}", Item{7}) != "7"; }
