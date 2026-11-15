// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace distill {
bool isStandardHeader(std::string_view headerName);
bool isStandardMacroName(std::string_view name);
bool isReserved(std::string_view name);
using ExtensionLists = std::map<std::string, std::set<std::string, std::less<>>, std::less<>>;
struct NameLists {
  ExtensionLists extensions; // D2c supplies generated, embedded library-specific data.
  bool protectedUserMacro(std::string_view name,
                          std::optional<std::string_view> library = {}) const;
};
} // namespace distill
