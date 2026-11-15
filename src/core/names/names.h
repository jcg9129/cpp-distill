// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/result.h"
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
using ExtensionNames = std::set<std::string, std::less<>>;
Result<ExtensionNames> parseExtensionList(std::string_view text, std::string_view file);
const ExtensionLists& embeddedExtensionLists();
struct NameLists {
  ExtensionLists extensions = embeddedExtensionLists();
  bool extensionListReady(std::optional<std::string_view> library = {}) const;
  bool protectedUserMacro(std::string_view name,
                          std::optional<std::string_view> library = {}) const;
};
} // namespace distill
