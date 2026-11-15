// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/result.h"
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace distill {
enum class TokenKind { identifier, number, literal, header, punctuation };
struct Token {
  TokenKind kind;
  std::string text; // Spliced spelling (raw contents restored), including delimiters.
  std::size_t begin, end, line; // Original byte offsets [begin,end), physical line.
};
struct Directive { std::string name; std::vector<Token> tokens; std::size_t begin, end; };
struct LogicalLine {
  std::size_t begin, end, firstLine, lastLine;
  std::string text; // Splices removed; CRLF and all other bytes retained.
  std::vector<Token> tokens;
  std::optional<Directive> directive;
};
struct ScanResult { std::vector<LogicalLine> lines; std::vector<Token> identifiers; };
// Lenient ordinary literals end at newline. Raw-token newlines stay inside their
// logical line; comment newlines terminate lines. Raw contents retain splices.
// %: at directive position is deliberately unsupported (an error).
Result<ScanResult> scanDirectives(std::string_view bytes);
} // namespace distill
