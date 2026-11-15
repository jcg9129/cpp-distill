// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/names/names.h"
#include <algorithm>
#include <iterator>

namespace distill {
namespace embedded {
extern const std::string_view headers[];
extern const std::size_t headersCount;
extern const std::string_view macros[];
extern const std::size_t macrosCount;
}
bool isStandardHeader(std::string_view name) {
  return name.starts_with("ext/pb_ds/") || std::binary_search(embedded::headers,
      embedded::headers + embedded::headersCount, name);
}
namespace {
bool numericWidth(std::string_view name) {
  const auto first = name.find_first_of("0123456789");
  if (first == name.npos) return false;
  auto end = first;
  while (end < name.size() && name[end] >= '0' && name[end] <= '9') ++end;
  const auto prefix = name.substr(0, first), suffix = name.substr(end);
  for (std::string_view p : {"INT", "UINT", "INT_LEAST", "UINT_LEAST", "INT_FAST", "UINT_FAST"})
    if (prefix == p && (suffix == "_MIN" || suffix == "_MAX" || suffix == "_WIDTH" || suffix == "_C")) return true;
  if (suffix.empty()) for (std::string_view p : {"PRI", "SCN"}) {
    if (!prefix.starts_with(p) || prefix.size() < 4) continue;
    if (std::string_view("diouxXbB").find(prefix[3]) == std::string_view::npos) continue;
    const auto type = prefix.substr(4);
    if (type.empty() || type == "LEAST" || type == "FAST") return true;
  }
  return false;
}
}
bool isStandardMacroName(std::string_view name) {
  return numericWidth(name) || std::binary_search(embedded::macros, embedded::macros + embedded::macrosCount, name);
}
bool isReserved(std::string_view name) {
  return name.find("__") != name.npos || (name.size() >= 2 && name[0] == '_' && name[1] >= 'A' && name[1] <= 'Z');
}
bool NameLists::protectedUserMacro(std::string_view name, std::optional<std::string_view> library) const {
  if (isReserved(name) || isStandardMacroName(name)) return false;
  if (library) {
    const auto found = extensions.find(*library);
    return found == extensions.end() || !found->second.contains(name);
  }
  return std::none_of(extensions.begin(), extensions.end(), [&](const auto& entry) { return entry.second.contains(name); });
}
} // namespace distill
