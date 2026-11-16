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
Result<bool> parseLineMarker(std::string_view, SourceLocation&, bool* system = nullptr);
Result<std::vector<PreprocessedToken>> parsePreprocessed(std::string_view, FileMapper = {});
// Error identifies the first difference, including both logical locations.
Result<bool> comparePreprocessed(std::string_view left, std::string_view right, FileMapper = {});
FileMapper originalFileMapper(const InlineResult&);
} // namespace distill
