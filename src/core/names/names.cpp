// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/names/names.h"
#include <algorithm>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace distill {
namespace embedded {
extern const std::string_view headers[];
extern const std::size_t headersCount;
extern const std::string_view protocols[], compiler[];
extern const std::size_t protocolsCount, compilerCount;
extern const std::string_view macros[];
extern const std::size_t macrosCount;
extern const std::string_view identityNames[], identityScopes[];
extern const std::size_t identityCount;
const std::string_view* extensionData(std::size_t index);
std::size_t extensionCount(std::size_t index);
}
const std::set<std::string, std::less<>>& shortenNamesCompilerNames() {
  static const std::set<std::string, std::less<>> names = [] {
    std::set<std::string, std::less<>> out;
    for (std::size_t i = 0; i < embedded::protocolsCount; ++i) out.emplace(embedded::protocols[i]);
    for (std::size_t i = 0; i < embedded::compilerCount; ++i) out.emplace(embedded::compiler[i]);
    return out;
  }();
  return names;
}
std::vector<std::string> standardHeaders() {
  return {embedded::headers, embedded::headers + embedded::headersCount};
}
bool isStandardHeader(std::string_view name) {
  if (name.starts_with('/')) return false;
#ifdef _WIN32
  if (name.find('\\') != name.npos ||
      (name.size() >= 2 && ((name[0] >= 'A' && name[0] <= 'Z') ||
                           (name[0] >= 'a' && name[0] <= 'z')) && name[1] == ':')) return false;
#endif
  for (std::size_t begin = 0; begin < name.size();) {
    const auto end = name.find('/', begin);
    if (name.substr(begin, end == name.npos ? name.size() - begin : end - begin) == "..") return false;
    if (end == name.npos) break;
    begin = end + 1;
  }
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
bool isIdentityMacroName(std::string_view name) {
  return std::binary_search(embedded::identityNames, embedded::identityNames + embedded::identityCount, name);
}
bool isIdentityMacro(std::string_view name, const std::vector<std::string>& libraries) {
  const auto* end = embedded::identityNames + embedded::identityCount;
  const auto* found = std::lower_bound(embedded::identityNames, end, name);
  if (found == end || *found != name) return false;
  const auto scope = embedded::identityScopes[found - embedded::identityNames];
  const auto covered = [&](const std::string& library) { return scope.find("|" + library + "|") != scope.npos; };
  if (libraries.empty()) {
    for (const char* library : {"glibc", "libstdc++", "mingw-w64", "macos-libc"})
      if (!covered(library)) return false;
    return true;
  }
  return std::all_of(libraries.begin(), libraries.end(), covered);
}
Result<ExtensionNames> parseExtensionList(std::string_view text, std::string_view file) {
  ExtensionNames names;
  std::istringstream lines{std::string(text)};
  std::string line, previous;
  std::size_t lineNumber = 0;
  while (std::getline(lines, line)) {
    ++lineNumber;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line.starts_with('#')) continue;
    const auto first = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; };
    if (!first(line[0]) || !std::all_of(line.begin() + 1, line.end(), [&](char c) { return first(c) || (c >= '0' && c <= '9'); }) ||
        isReserved(line) || isStandardMacroName(line) || (!previous.empty() && line <= previous))
      return Error{std::string(file) + ":" + std::to_string(lineNumber) + ": invalid, reserved, standard, duplicate or unsorted extension name"};
    names.insert(line); previous = line;
  }
  return names;
}
const ExtensionLists& embeddedExtensionLists() {
  static const ExtensionLists lists = [] {
    ExtensionLists result;
    std::size_t i = 0;
    for (const char* library : {"glibc", "libstdc++", "mingw-w64", "macos-libc"}) {
      std::string text;
      const auto* entries = embedded::extensionData(i);
      const auto count = embedded::extensionCount(i++);
      for (std::size_t n = 0; n < count; ++n) { text += entries[n]; text += '\n'; }
      auto parsed = parseExtensionList(text, library);
      if (!parsed.ok()) throw std::runtime_error(parsed.error().message); // Invalid build-time data.
      result.emplace(library, std::move(parsed.value()));
    }
    return result;
  }();
  return lists;
}
bool NameLists::extensionListReady(const std::vector<std::string>& libraries) const {
  if (libraries.empty()) return !extensions.empty() && std::all_of(extensions.begin(), extensions.end(), [](const auto& entry) { return !entry.second.empty(); });
  return std::all_of(libraries.begin(), libraries.end(), [&](const auto& name) { const auto it = extensions.find(name); return it != extensions.end() && !it->second.empty(); });
}
bool NameLists::protectedUserMacro(std::string_view name, const std::vector<std::string>& libraries) const {
  if (isReserved(name) || isStandardMacroName(name) || !extensionListReady(libraries)) return false;
  if (libraries.empty()) return std::none_of(extensions.begin(), extensions.end(), [&](const auto& entry) { return entry.second.contains(name); });
  return std::none_of(libraries.begin(), libraries.end(), [&](const auto& library) { return extensions.at(library).contains(name); });
}
} // namespace distill
