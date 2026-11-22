// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/inline/inline.h"
#include <functional>

namespace distill {
struct PreprocessedToken {
  std::string text;
  SourceLocation location;
  bool pragmaBoundary = false;
  bool operator==(const PreprocessedToken&) const = default;
};
using FileMapper = std::function<std::string(std::string_view)>;
// False means an ordinary line; malformed numeric markers return an error.
// A marker without a file name keeps the location's file and the system flag
// ([cpp.line]); `named` reports which form was read.
Result<bool> parseLineMarker(std::string_view, SourceLocation&, bool* system = nullptr, bool* named = nullptr);
// Presumed file name established by generated markers, in textual order
// (§4.4 R4-G). A marker omits its file name only when the previous generated
// marker names that file and no directive other than #define, #undef or
// #pragma lies between them: then both lie in the same conditional group, and
// every path processing the marker has that presumed name. Emitters and
// readers of generated markers feed it every line kind in textual order.
class PresumedFile {
public:
  // Every directive line that is not a generated marker.
  void directive(std::string_view name);
  // Spelling (with newline) of a generated marker here; it takes effect.
  std::string marker(const SourceLocation&);
  // Reads a generated marker here; a short one must satisfy the rule above.
  Result<bool> parse(std::string_view line, SourceLocation&);
private:
  std::optional<std::string> file;
};
Result<std::vector<PreprocessedToken>> parsePreprocessed(std::string_view, FileMapper = {});
// Error identifies the first difference, including both logical locations.
Result<bool> comparePreprocessed(std::string_view left, std::string_view right, FileMapper = {});
// --no-line-markers and --compact-whitespace: -E before and after have the
// same tokens and #pragma boundaries; tokens whose presumed file is the same in
// both (system headers) also keep their line.
Result<bool> compareUnmarkedPreprocessed(std::string_view marked, std::string_view unmarked);
FileMapper originalFileMapper(const InlineResult&);
} // namespace distill
