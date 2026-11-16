// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/prune/prune.h"

namespace distill {
struct CopiedSpan { EditRange input; std::size_t output; };
struct RenderedSource {
  // All physical offsets/ranges in this provenance refer to the rendered text.
  // Removed occurrences disappear; retained ranges can split around markers.
  PruningModel provenance;
  std::vector<CopiedSpan> copied;
  std::optional<std::size_t> mapOffset(std::size_t) const;
};
// A null retention renders without declaration deletion (raw/flat stripping).
// Generated markers are identified by D2's null line-map entries, not spelling.
Result<RenderedSource> renderSource(const PruningModel&, const PruningResult* = nullptr,
                                    bool stripComments = false);
// Independently re-lexes actual bytes, checking tokens, locations and directive
// boundaries/order against the immutable input minus the requested deletions.
Result<bool> renderingInvariant(const PruningModel&, const PruningResult*, std::string_view);
} // namespace distill
