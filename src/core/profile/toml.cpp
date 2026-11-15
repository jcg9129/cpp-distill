// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/profile/toml.h"
#include <charconv>
#include <limits>

namespace distill {
namespace {
struct Parser {
  std::string_view text, file;
  std::size_t pos = 0, line = 1;
  [[noreturn]] void fail(std::string message) const {
    throw Error{std::string(file) + ":" + std::to_string(line) + ": " + message};
  }
  char peek() const { return pos < text.size() ? text[pos] : '\0'; }
  bool take(char c) { if (pos < text.size() && text[pos] == c) { ++pos; return true; } return false; }
  void utf8() const {
    std::size_t atLine = 1;
    for (std::size_t i = 0; i < text.size();) {
      auto bad = [&] { throw Error{std::string(file) + ":" + std::to_string(atLine) + ": invalid UTF-8 or control character"}; };
      const auto c = static_cast<unsigned char>(text[i++]);
      if (c < 128) {
        if (c == '\n') ++atLine;
        if ((c < 32 && c != '\n' && c != '\r' && c != '\t') || c == 127) bad();
        if (c == '\r' && (i == text.size() || text[i] != '\n')) bad();
        continue;
      }
      unsigned count = 0, cp = 0, minimum = 0;
      if (c >= 0xc2 && c <= 0xdf) { count = 1; cp = c & 31; minimum = 0x80; }
      else if (c >= 0xe0 && c <= 0xef) { count = 2; cp = c & 15; minimum = 0x800; }
      else if (c >= 0xf0 && c <= 0xf4) { count = 3; cp = c & 7; minimum = 0x10000; }
      else bad();
      while (count--) {
        if (i == text.size()) bad();
        const auto next = static_cast<unsigned char>(text[i++]);
        if ((next & 0xc0) != 0x80) bad();
        cp = cp * 64 + (next & 63);
      }
      if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) bad();
    }
  }
  void spaces() { while (peek() == ' ' || peek() == '\t') ++pos; }
  bool newline() {
    if (take('\r')) { if (!take('\n')) fail("expected LF after CR"); ++line; return true; }
    if (take('\n')) { ++line; return true; }
    return false;
  }
  void comment() { if (take('#')) while (pos < text.size() && peek() != '\n' && peek() != '\r') ++pos; }
  void gaps() { do { spaces(); comment(); } while (newline()); }
  static void appendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) { out += static_cast<char>(0xc0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 63)); }
    else if (cp < 0x10000) {
      out += static_cast<char>(0xe0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 63)); out += static_cast<char>(0x80 | (cp & 63));
    } else {
      out += static_cast<char>(0xf0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 63));
      out += static_cast<char>(0x80 | ((cp >> 6) & 63)); out += static_cast<char>(0x80 | (cp & 63));
    }
  }
  std::string string() {
    const char quote = peek();
    if (quote != '\'' && quote != '"') fail("expected a string");
    ++pos;
    std::string out;
    while (pos < text.size() && peek() != quote) {
      char c = text[pos++];
      if (c == '\n' || c == '\r') fail("multiline strings are not supported");
      if (quote == '"' && c == '\\') {
        if (pos == text.size()) fail("unfinished escape");
        c = text[pos++];
        switch (c) {
        case '\\': out += '\\'; break;
        case '"': out += '"'; break;
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'u': case 'U': {
          unsigned cp = 0;
          for (unsigned n = (c == 'u' ? 4 : 8); n; --n) {
            if (pos == text.size()) fail("incomplete Unicode escape");
            const char h = text[pos++];
            const int digit = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1;
            if (digit < 0) fail("invalid Unicode escape");
            cp = (cp << 4) | static_cast<unsigned>(digit);
          }
          if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) fail("invalid Unicode scalar");
          appendUtf8(out, cp);
          break;
        }
        default: fail("unsupported escape");
        }
      } else out += c;
    }
    if (!take(quote)) fail("unterminated string");
    return out;
  }
  static bool bare(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; }
  std::string key() {
    if (peek() == '"' || peek() == '\'') return string();
    const auto start = pos;
    while (bare(peek())) ++pos;
    if (pos == start) fail("expected a bare or quoted key (tables and dotted keys are not supported)");
    return std::string(text.substr(start, pos - start));
  }
  TomlValue value() {
    if (peek() == '"' || peek() == '\'') return string();
    if (take('[')) {
      TomlStrings array;
      gaps();
      if (take(']')) return array;
      for (;;) {
        array.push_back(string()); gaps();
        if (take(']')) return array;
        if (!take(',')) fail("expected ',' or ']' in string array");
        gaps(); if (take(']')) return array;
      }
    }
    if (take('{')) {
      TomlTable table;
      spaces(); if (take('}')) return table;
      for (;;) {
        auto k = key(); spaces();
        if (!take('=')) fail("expected '=' in inline table");
        spaces(); auto v = string();
        if (!table.emplace(std::move(k), std::move(v)).second) fail("duplicate inline-table key");
        spaces(); if (take('}')) return table;
        if (!take(',')) fail("expected ',' or '}' in inline table");
        spaces(); // Inline tables are single-line and do not allow a trailing comma.
      }
    }
    const auto start = pos;
    while (pos < text.size() && peek() != ' ' && peek() != '\t' && peek() != '\r' && peek() != '\n' && peek() != '#') ++pos;
    const auto token = text.substr(start, pos - start);
    if (token == "true") return true;
    if (token == "false") return false;
    auto digits = token;
    bool negative = false, sign = false;
    if (!digits.empty() && (digits[0] == '+' || digits[0] == '-')) {
      negative = digits[0] == '-'; sign = true; digits.remove_prefix(1);
    }
    int base = 10;
    if (digits.starts_with("0x") || digits.starts_with("0o") || digits.starts_with("0b")) {
      if (sign) fail("non-decimal integers cannot have a sign");
      base = digits[1] == 'x' ? 16 : digits[1] == 'o' ? 8 : 2;
      digits.remove_prefix(2);
    }
    if (digits.empty() || (base == 10 && digits.size() > 1 && digits[0] == '0')) fail("expected a subset value");
    std::string normalized;
    bool previousDigit = false;
    for (std::size_t i = 0; i < digits.size(); ++i) {
      const char c = digits[i];
      if (c == '_') {
        if (!previousDigit || i + 1 == digits.size()) fail("misplaced integer separator");
        previousDigit = false; continue;
      }
      const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
      if (digit < 0 || digit >= base) fail("expected a subset value (no dates or floats)");
      normalized += c; previousDigit = true;
    }
    std::uint64_t magnitude = 0;
    const auto parsed = std::from_chars(normalized.data(), normalized.data() + normalized.size(), magnitude, base);
    const auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (parsed.ec != std::errc{} || magnitude > maximum + (negative ? 1u : 0u)) fail("integer out of range");
    const auto number = negative ? (magnitude == maximum + 1 ? std::numeric_limits<std::int64_t>::min() : -static_cast<std::int64_t>(magnitude)) : static_cast<std::int64_t>(magnitude);
    return number;
  }
  TomlDocument parse() {
    utf8();
    if (text.starts_with("\xef\xbb\xbf")) pos = 3;
    TomlDocument document;
    gaps();
    while (pos < text.size()) {
      const auto at = line;
      auto k = key(); spaces();
      if (!take('=')) fail("expected '=' after key");
      spaces(); auto v = value();
      if (!document.emplace(std::move(k), TomlEntry{std::move(v), at}).second) fail("duplicate key");
      spaces(); comment();
      if (pos < text.size() && !newline()) fail("expected end of assignment");
      gaps();
    }
    return document;
  }
};
} // namespace
Result<bool> validateProfileUtf8(std::string_view text, std::string_view file) {
  try { Parser{text, file}.utf8(); return true; }
  catch (const Error& error) { return error; }
}
Result<TomlDocument> parseProfileToml(std::string_view text, std::string_view file) {
  try { return Parser{text, file}.parse(); }
  catch (const Error& error) { return error; }
  catch (const std::exception& error) { return Error{std::string(file) + ":1: " + error.what()}; }
}
} // namespace distill
