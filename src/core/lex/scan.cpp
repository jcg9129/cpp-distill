// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/lex/scan.h"
#include <algorithm>
#include <cctype>

namespace distill {
namespace {
bool idStart(unsigned char c) { return std::isalpha(c) || c == '_' || c >= 128; }
bool idRest(unsigned char c) { return idStart(c) || std::isdigit(c); }
std::size_t spliceEnd(std::string_view bytes, std::size_t i) {
  if (bytes[i] != '\\') return i;
  auto n = i + 1;
  while (n < bytes.size() && (bytes[n] == ' ' || bytes[n] == '\t' || bytes[n] == '\f' || bytes[n] == '\v')) ++n;
  if (n < bytes.size() && bytes[n] == '\r' && n + 1 < bytes.size() && bytes[n + 1] == '\n') ++n;
  return n < bytes.size() && bytes[n] == '\n' ? n + 1 : i;
}
Result<ScanResult> scan(std::string_view bytes) {
  std::string s;
  std::vector<std::size_t> offsets, physical, owners;
  ScanResult result;
  std::size_t line = 1, start = 0, first = 1;
  for (std::size_t i = 0; i < bytes.size();) {
    const auto end = spliceEnd(bytes, i);
    if (end != i) { i = end; ++line; continue; }
    offsets.push_back(i); physical.push_back(line); owners.push_back(result.lines.size());
    s += bytes[i++];
    if (s.back() == '\n') {
      result.lines.push_back({start, i, first, line, {}, {}, {}});
      start = i; first = ++line;
    }
  }
  if (start < bytes.size()) result.lines.push_back({start, bytes.size(), first, bytes.back() == '\n' ? line - 1 : line, {}, {}, {}});
  // A file consisting only of a final splice still has a logical line.
  for (std::size_t i = 0; i < s.size(); ++i) result.lines[owners[i]].text += s[i];
  // Each phase-2 line initially owns itself. Comments and raw tokens join
  // covered lines before any following token is classified.
  std::vector<std::size_t> joined(result.lines.size());
  for (std::size_t i = 0; i < joined.size(); ++i) joined[i] = i;
  auto join = [&](std::size_t a, std::size_t b) {
    const auto owner = joined[owners[a]];
    for (auto n = owners[a] + 1; n <= owners[b - 1]; ++n) joined[n] = owner;
  };
  std::vector<bool> occupied(result.lines.size(), false), directiveStart(result.lines.size(), false);
  auto emit = [&](TokenKind kind, std::size_t a, std::size_t b, std::optional<std::string> spelling = {}) {
    Token token{kind, spelling ? std::move(*spelling) : s.substr(a, b - a), offsets[a], offsets[b - 1] + 1, physical[a]};
    auto& tokens = result.lines[joined[owners[a]]].tokens;
    if (!occupied[joined[owners[a]]] && token.text == "#") directiveStart[joined[owners[a]]] = true;
    tokens.push_back(token);
    if (kind == TokenKind::identifier) result.identifiers.push_back(token);
    occupied[joined[owners[a]]] = true;
  };
  for (std::size_t i = 0; i < s.size();) {
    if (i == 0 && s.starts_with("\xef\xbb\xbf")) { i += 3; continue; }
    if (std::isspace(static_cast<unsigned char>(s[i]))) { ++i; continue; }
    if (s.compare(i, 2, "//") == 0) {
      const auto a = i;
      const auto end = s.find('\n', i + 2); i = end == s.npos ? s.size() : end;
      result.comments.push_back({offsets[a], i < offsets.size() ? offsets[i] : bytes.size()}); continue;
    }
    if (s.compare(i, 2, "/*") == 0) {
      const auto a = i;
      const auto end = s.find("*/", i + 2); i = end == s.npos ? s.size() : end + 2;
      result.comments.push_back({offsets[a], offsets[i - 1] + 1});
      join(a, i); continue;
    }
    const auto a = i;
    const auto owner = joined[owners[i]];
    auto& tokens = result.lines[owner].tokens;
    if (!occupied[owner] && s.compare(i, 2, "%:") == 0)
      return Error{std::to_string(physical[i]) + ": digraph %: directive introducer is unsupported"};
    // Header names are single pp-tokens: comment/escape syntax has no meaning inside.
    const bool includeOperand = tokens.size() == 2 && tokens[0].text == "#" && tokens[1].text == "include";
    const bool hasOperand = tokens.size() >= 2 && tokens.back().text == "(" &&
        (tokens[tokens.size() - 2].text == "__has_include" || tokens[tokens.size() - 2].text == "__has_include_next");
    if ((includeOperand || hasOperand) && (s[i] == '<' || s[i] == '"')) {
      const char end = s[i] == '<' ? '>' : '"';
      ++i;
      while (i < s.size() && s[i] != '\n' && s[i] != end) ++i;
      if (i < s.size() && s[i] == end) { emit(TokenKind::header, a, ++i); continue; }
      i = a; // Malformed operand: tokenize leniently; the inliner will reject it.
    }
    std::size_t prefix = 0;
    bool raw = false, literal = false;
    for (std::string_view p : {"u8R\"", "LR\"", "uR\"", "UR\"", "R\"", "u8\"", "u8'", "L\"", "L'", "u\"", "u'", "U\"", "U'", "\"", "'"}) {
      if (s.compare(i, p.size(), p) == 0) { prefix = p.size(); raw = p.find('R') != p.npos; literal = true; break; }
    }
    if (literal) {
      std::optional<std::string> spelling;
      if (raw) {
        const auto open = s.find('(', i + prefix);
        if (open != s.npos && open - i - prefix <= 16 &&
            s.substr(i + prefix, open - i - prefix).find_first_of(" \t\r\n\\)") == s.npos) {
          const auto close = ")" + s.substr(i + prefix, open - i - prefix) + "\"";
          // Raw-string contents revert phase-2 splicing. A close delimiter
          // synthesized by a splice inside the body must not terminate it.
          const auto end = bytes.find(close, offsets[open] + 1);
          const auto rawEnd = end == bytes.npos ? bytes.size() : end + close.size();
          i = static_cast<std::size_t>(std::lower_bound(offsets.begin(), offsets.end(), rawEnd) - offsets.begin());
          spelling = s.substr(a, open + 1 - a);
          for (auto pos = offsets[open] + 1; pos < rawEnd;) {
            const auto end = spliceEnd(bytes, pos);
            if (end != pos) {
              // GCC restores whitespace splices in raw contents as a backslash,
              // one space, and a newline, regardless of whitespace kind.
              *spelling += '\\';
              const auto newline = end >= 2 && bytes[end - 2] == '\r' ? end - 2 : end - 1;
              if (newline != pos + 1) *spelling += ' ';
              *spelling += '\n';
              pos = end;
            } else {
              // Phase 1 newline normalization is not reverted by raw strings.
              if (bytes[pos] == '\r' && pos + 1 < rawEnd && bytes[pos + 1] == '\n') ++pos;
              *spelling += bytes[pos++];
            }
          }
        } else { i += prefix; while (i < s.size() && s[i] != '\n') ++i; }
      } else {
        const char quote = s[i + prefix - 1]; i += prefix;
        while (i < s.size() && s[i] != '\n') {
          if (s[i] == quote) { ++i; break; }
          if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] != '\n') i += 2;
          else ++i;
        }
      }
      join(a, i);
      emit(TokenKind::literal, a, i, std::move(spelling)); continue;
    }
    if (idStart(static_cast<unsigned char>(s[i]))) {
      while (++i < s.size() && idRest(static_cast<unsigned char>(s[i]))) {}
      emit(TokenKind::identifier, a, i); continue;
    }
    if (std::isdigit(static_cast<unsigned char>(s[i])) || (s[i] == '.' && i + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[i + 1])))) {
      ++i;
      while (i < s.size()) {
        if (idRest(static_cast<unsigned char>(s[i])) || s[i] == '.') { ++i; continue; }
        if (s[i] == '\'' && i + 1 < s.size() && idRest(static_cast<unsigned char>(s[i + 1]))) { i += 2; continue; }
        if ((s[i] == '+' || s[i] == '-') && (s[i - 1] == 'e' || s[i - 1] == 'E' || s[i - 1] == 'p' || s[i - 1] == 'P')) { ++i; continue; }
        break;
      }
      emit(TokenKind::number, a, i); continue;
    }
    std::size_t width = 1;
    for (std::string_view p : {"%:%:", "<=>", ">>=", "<<=", "->*", "...", "##", "::", ".*", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||", "*=", "/=", "%=", "+=", "-=", "&=", "^=", "|=", "<:", ":>", "<%", "%>", "%:"}) {
      if (s.compare(i, p.size(), p) == 0) { width = p.size(); break; }
    }
    // [lex.pptoken]'s <:: exception (e.g. vector<::X>).
    if (s.compare(i, 3, "<::") == 0 && (i + 3 == s.size() || (s[i + 3] != ':' && s[i + 3] != '>'))) width = 1;
    i += width;
    emit(TokenKind::punctuation, a, i);
  }
  // Phase 3 replaces a whole block comment with a space. Its newlines,
  // like those inside a raw token, cannot terminate a directive.
  auto lines = std::move(result.lines);
  for (std::size_t i = 0; i < lines.size(); ++i) {
    const auto owner = i;
    const bool directive = directiveStart[i];
    auto l = std::move(lines[i]);
    while (i + 1 < lines.size() && joined[i + 1] == owner) {
      auto& next = lines[++i];
      l.end = next.end; l.lastLine = next.lastLine; l.text += next.text;
    }
    if (directive) {
      const auto& t = l.tokens;
      l.directive = Directive{t.size() > 1 ? t[1].text : "", {}, t[0].begin, l.end};
      if (t.size() > 2) l.directive->tokens.assign(t.begin() + 2, t.end());
    }
    result.lines.push_back(std::move(l));
  }
  return result;
}
} // namespace
Result<ScanResult> scanDirectives(std::string_view bytes) {
  try { return scan(bytes); }
  catch (const std::exception& e) { return Error{std::string("scanner: ") + e.what()}; }
}
} // namespace distill
