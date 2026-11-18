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

// --no-line-markers (docs/reviews/2026-11-18-no-line-markers.md). Reasons the
// program may observe presumed source positions, from the checked flat
// parse model and the checking argv; empty means markers may be omitted.
std::vector<std::string> lineMarkerObservations(const PruningModel& flat, const std::vector<std::string>& argv);
// The rendered text without its generated marker lines (each must be a whole
// #line line). Positions are not mapped: the result is a final artifact.
Result<std::string> withoutLineMarkers(const RenderedSource&);
// Independent re-lex: marked minus its #line lines and unmarked have the same
// tokens, kinds and directive boundaries; unmarked has no line directive.
Result<bool> unmarkedInvariant(std::string_view marked, std::string_view unmarked);
} // namespace distill
