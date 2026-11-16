// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/names/adl.h"
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace distill {
namespace embedded { extern const std::string_view adl, extensionCoverage, adlDigest; }
namespace {
std::string normalized(std::string_view s) {
  for (const auto* d : {"11", "14", "17"})
    if (s == std::string("c++") + d || s == std::string("gnu++") + d) return std::string("c++") + d;
  if (s == "gnu++20" || s == "c++20") return "c++20";
  if (s == "gnu++23" || s == "c++23") return "c++23";
  return {};
}
}
bool AdlRegistry::supports(std::string_view dialect) const {
  auto d = normalized(dialect);
  if (d.empty()) return false;
  for (const auto& e : entries) if (e.dialect == d) return true;
  return false;
}
std::set<std::string, std::less<>> AdlRegistry::names(std::string_view dialect) const {
  std::set<std::string, std::less<>> result;
  for (const auto& e : entries) if (e.dialect == normalized(dialect)) result.insert(e.name);
  return result;
}
Result<AdlRegistry> parseAdlRegistry(std::string_view text) {
  AdlRegistry r;
  std::istringstream input{std::string(text)}; std::string line;
  std::set<std::string> keys;
  while (std::getline(input, line)) {
    if (line.starts_with("# version: ")) r.version = line.substr(11);
    if (line.empty() || line.starts_with('#')) continue;
    std::istringstream fields(line); AdlEntry e; std::string extra;
    if (!(fields >> e.dialect >> e.name >> e.mechanism >> e.clause >> e.publicName >> e.verification) ||
        (fields >> extra) || normalized(e.dialect) != e.dialect || e.clause.front() != '[' ||
        (e.verification != "verified" && e.verification != "UNVERIFIED") ||
        !keys.insert(e.dialect + ":" + e.name + ":" + e.clause).second)
      return Error{"invalid ADL registry row: " + line};
    r.entries.push_back(std::move(e));
  }
  if (r.version.empty()) return Error{"missing ADL registry version"};
  for (const auto* d : {"c++11", "c++14", "c++17", "c++20", "c++23"})
    if (!r.supports(d)) return Error{"incomplete ADL registry: " + std::string(d)};
  return r;
}
const AdlRegistry& embeddedAdlRegistry() {
  static const auto r = [] {
    auto parsed = parseAdlRegistry(embedded::adl);
    if (!parsed.ok()) throw std::runtime_error(parsed.error().message);
    parsed.value().digest = std::string(embedded::adlDigest);
    return std::move(parsed.value());
  }();
  return r;
}
std::string_view embeddedExtensionCoverage() { return embedded::extensionCoverage; }
bool extensionPruningCovered(std::string_view header) {
  // No extension is certified by version 1. Unknown extensions fail closed.
  return !header.starts_with("ext/") && header != "bits/extc++.h";
}
} // namespace distill
