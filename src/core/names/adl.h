// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/result.h"
#include <set>
#include <string>
#include <string_view>
#include <vector>
namespace distill {
struct AdlEntry { std::string dialect, name, mechanism, clause, publicName, verification; };
struct AdlRegistry {
  std::string version, digest;
  std::vector<AdlEntry> entries;
  bool supports(std::string_view dialect) const;
  std::set<std::string, std::less<>> names(std::string_view dialect) const;
};
Result<AdlRegistry> parseAdlRegistry(std::string_view);
const AdlRegistry& embeddedAdlRegistry();
std::string_view embeddedExtensionCoverage();
bool extensionPruningCovered(std::string_view header);
} // namespace distill
