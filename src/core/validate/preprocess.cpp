// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/validate/preprocess.h"
#include "core/lex/scan.h"
#include "core/path.h"
#include <charconv>
#include <set>

namespace distill {
Result<bool> parseLineMarker(std::string_view s, SourceLocation& at, bool* system, bool* named) {
  auto spaces = [&] { while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1); };
  spaces();
  if (!s.starts_with('#')) return false;
  s.remove_prefix(1); spaces();
  if (s.starts_with("line ")) { s.remove_prefix(5); spaces(); }
  if (s.empty() || s.front() < '0' || s.front() > '9') return false;
  std::size_t line = 0;
  auto n = std::from_chars(s.data(), s.data() + s.size(), line);
  if (n.ec != std::errc{}) return Error{"invalid preprocessor line number"};
  s.remove_prefix(n.ptr - s.data()); spaces();
  if (s.empty()) {
    at.line = line;
    if (named) *named = false;
    return true;
  }
  if (!s.starts_with('"')) return Error{"missing preprocessor file name"};
  s.remove_prefix(1); std::string name;
  bool closed = false;
  while (!s.empty()) {
    char c = s.front(); s.remove_prefix(1);
    if (c == '"') { closed = true; break; }
    if (c == '\\') {
      if (s.empty()) return Error{"incomplete file name escape"};
      c = s.front(); s.remove_prefix(1);
      if (c >= '0' && c <= '7') {
        unsigned value = c - '0';
        for (int i = 0; i < 2 && !s.empty() && s.front() >= '0' && s.front() <= '7'; ++i) {
          value = value * 8 + s.front() - '0'; s.remove_prefix(1);
        }
        c = static_cast<char>(value);
      } else if (c != '\\' && c != '"') return Error{"unsupported file name escape"};
    }
    name += c;
  }
  if (!closed) return Error{"unterminated preprocessor file name"};
  spaces();
  bool isSystem = false;
  while (!s.empty()) {
    if (s.front() < '1' || s.front() > '4') return Error{"invalid preprocessor marker flag"};
    isSystem |= s.front() == '3';
    s.remove_prefix(1);
    if (!s.empty() && s.front() != ' ' && s.front() != '\t' && s.front() != '\r') return Error{"invalid marker flag boundary"};
    spaces();
  }
  at = {std::move(name), line};
  if (system) *system = isSystem;
  if (named) *named = true;
  return true;
}
void PresumedFile::directive(std::string_view name) {
  if (name != "define" && name != "undef" && name != "pragma") file.reset();
}
std::string PresumedFile::marker(const SourceLocation& at) {
  auto text = "#line " + std::to_string(at.line);
  if (file != at.file) {
    text += " \"";
    for (char c : at.file) { if (c == '\\' || c == '"') text += '\\'; text += c; }
    text += '"';
    file = at.file;
  }
  return text + '\n';
}
Result<bool> PresumedFile::parse(std::string_view line, SourceLocation& at) {
  bool named = false;
  auto parsed = parseLineMarker(line, at, nullptr, &named);
  if (!parsed.ok() || !parsed.value()) return parsed;
  if (!named) {
    if (!file) return Error{"short line marker without a preceding marker in its segment"};
    at.file = *file;
  }
  file = at.file;
  return true;
}
Result<std::vector<PreprocessedToken>> parsePreprocessed(std::string_view output, FileMapper map) {
  // Scan before interpreting markers: a marker-shaped line in a raw literal is text.
  std::set<std::size_t> directives;
  auto scanned = scanDirectives(output);
  if (!scanned.ok()) return scanned.error();
  for (const auto& l : scanned.value().lines) if (l.directive) directives.insert(l.firstLine);
  // Reuse this scan instead of constructing and scanning a second full buffer.
  // Identifier duplicates are unnecessary for preprocess comparison.
  scanned.value().identifiers.clear();
  std::set<std::size_t> markers;
  std::vector<SourceLocation> locations;
  SourceLocation at{"<initial>", 1};
  bool system = false;
  std::size_t physical = 1;
  for (std::size_t begin = 0; begin < output.size(); ++physical) {
    auto end = output.find('\n', begin);
    if (end == output.npos) end = output.size();
    auto line = output.substr(begin, end - begin);
    bool marker = false;
    if (directives.contains(physical)) {
      auto parsed = parseLineMarker(line, at, &system);
      if (!parsed.ok()) return parsed.error();
      marker = parsed.value();
    }
    if (!marker) locations.push_back({map && !system ? map(at.file) : at.file, at.line++});
    else { locations.push_back({"<marker>", 0}); markers.insert(physical); }
    begin = end + (end < output.size());
  }
  std::vector<PreprocessedToken> result;
  for (auto& l : scanned.value().lines) {
    if (markers.contains(l.firstLine)) continue;
    if (l.directive && l.directive->name != "pragma") return Error{"unexpected directive in -E output: #" + l.directive->name};
    const bool pragma = l.directive.has_value();
    if (pragma) result.push_back({"<pragma-start>", locations.at(l.firstLine - 1), true});
    for (auto& t : l.tokens) result.push_back({std::move(t.text), locations.at(t.line - 1), false});
    if (pragma) result.push_back({"<pragma-end>", locations.at(l.lastLine - 1), true});
  }
  return result;
}
Result<bool> comparePreprocessed(std::string_view left, std::string_view right, FileMapper map) {
  auto a = parsePreprocessed(left, map), b = parsePreprocessed(right, map);
  if (!a.ok()) return a.error();
  if (!b.ok()) return b.error();
  std::size_t i = 0;
  while (i < a.value().size() && i < b.value().size() && a.value()[i] == b.value()[i]) ++i;
  if (i == a.value().size() && i == b.value().size()) return true;
  auto show = [i](const auto& ts) {
    if (i == ts.size()) return std::string("<end>");
    const auto& t = ts[i]; return t.text + " at " + t.location.file + ":" + std::to_string(t.location.line);
  };
  return Error{"preprocess difference at token " + std::to_string(i) + ": left " + show(a.value()) + "; right " + show(b.value())};
}
Result<bool> compareUnmarkedPreprocessed(std::string_view marked, std::string_view unmarked) {
  auto a = parsePreprocessed(marked), b = parsePreprocessed(unmarked);
  if (!a.ok()) return a.error();
  if (!b.ok()) return b.error();
  const auto& x = a.value(); const auto& y = b.value();
  for (std::size_t i = 0; i < x.size() || i < y.size(); ++i) {
    auto show = [i](const auto& ts) {
      if (i == ts.size()) return std::string("<end>");
      const auto& t = ts[i]; return t.text + " at " + t.location.file + ":" + std::to_string(t.location.line);
    };
    if (i == x.size() || i == y.size() || x[i].text != y[i].text || x[i].pragmaBoundary != y[i].pragmaBoundary ||
        (x[i].location.file == y[i].location.file && x[i].location.line != y[i].location.line))
      return Error{"unmarked preprocess difference at token " + std::to_string(i) + ": marked " + show(x) + "; unmarked " + show(y)};
  }
  return true;
}
FileMapper originalFileMapper(const InlineResult& raw) {
  std::map<std::string, std::string, std::less<>> names;
  std::map<FileIdentity, std::string> representatives;
  for (const auto& f : raw.userFiles) {
    const auto id = representatives.try_emplace(f.identity, f.fileName).first->second;
    names[f.fileName] = id;
    names[pathToUtf8(f.path)] = id;
  }
  return [names = std::move(names)](std::string_view name) {
    auto found = names.find(name);
    return found == names.end() ? std::string(name) : found->second;
  };
}
} // namespace distill
