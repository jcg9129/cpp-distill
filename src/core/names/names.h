// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/result.h"
#include <map>
#include <vector>
#include <set>
#include <string>
#include <string_view>

namespace distill {
std::vector<std::string> standardHeaders();
bool isStandardHeader(std::string_view headerName);
bool isStandardMacroName(std::string_view name);
bool isReserved(std::string_view name);
// Empty inventories require coverage by every supported library.
bool isIdentityMacro(std::string_view name, const std::vector<std::string>& libraries = {});
bool isIdentityMacroName(std::string_view name);
using ExtensionLists = std::map<std::string, std::set<std::string, std::less<>>, std::less<>>;
using ExtensionNames = std::set<std::string, std::less<>>;
Result<ExtensionNames> parseExtensionList(std::string_view text, std::string_view file);
const ExtensionLists& embeddedExtensionLists();
struct NameLists {
  ExtensionLists extensions = embeddedExtensionLists();
  bool extensionListReady(const std::vector<std::string>& libraries = {}) const;
  bool protectedUserMacro(std::string_view name,
                          const std::vector<std::string>& libraries = {}) const;
};
} // namespace distill
