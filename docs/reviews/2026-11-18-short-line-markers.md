<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Short generated `#line` markers (2026-11-18)

## Verdict

**Sound with the segment rule below; implemented.** A generated marker is
spelled `#line N` (no file name) exactly when the previous generated marker in
textual order names the same file and no other directive except `#define`,
`#undef` or `#pragma` lies between the two. Every other marker, including the
first one of an output, keeps `#line N "file"`. No problem without a sound fix
was found.

## Semantics

[cpp.line]/3–4: `#line digit-sequence` sets the presumed line number and keeps
the presumed file name; `#line digit-sequence "s-char-sequence"` sets both. So
`#line N` is equivalent to `#line N "F"` exactly at points where the presumed
file name already is `F` on every preprocessing path that processes the marker.
GCC (`do_line`) and Clang (`AddLineNote` with no file id) implement the short
form by reusing the current presumed file name; neither changes the system
header flag or anything else depending on which form was used. Hence
`__FILE__`, `__LINE__`, `std::source_location`, `__builtin_FILE` and `-E`
linemarkers are unchanged.

## Segment rule and its proof

Let `M2` be a short marker and `M1` the previous generated marker in textual
order, naming `F`. Between them there is no conditional directive (`#if`,
`#ifdef`, `#ifndef`, `#elif*`, `#else`, `#endif`), no `#include`-like
directive and no source `#line` (user `#line` is rejected anyway, §1.2).

1. With no conditional directive between them, `M1` and `M2` lie in the same
   group of the same conditional nesting; the preprocessor skips both or
   processes both. So every path that processes `M2` processed `M1`.
2. Only `#line` directives change the presumed name of the current source file.
   No marker lies between `M1` and `M2` (`M1` is the nearest), and there is no
   source `#line`. An `#include` could only change it inside the included file
   (after return the includer's name is restored), but we reset on it anyway,
   so the argument does not depend on that.
3. Therefore the presumed name at `M2` is `F` on every path, and `#line N`
   equals `#line N "F"`.

The first marker never has a predecessor, so it keeps its name (the presumed
name at the start of `pruned.cpp` is `pruned.cpp`, not an original file).

Consequences checked in the code:

- A marker right after a kept unknown `#if`/`#else`/`#endif` keeps its name,
  even when the presumed name is in fact the same on all paths (renderer
  `forceMarker`, inliner/resolver resync markers). This loses a little size and
  needs no path analysis; a dataflow over the group structure would allow more,
  but every textual consumer (below) would then need the same dataflow.
- Kept system `#include`s reset the state too (conservative).
- Markers are placed by the existing logic (line starts outside directives,
  comments and raw strings); only their spelling changes.

## Stages and consumers

Markers flow inline → resolve → (strip) → prune/render. Each stage now
re-spells every marker it outputs from its own output's state, instead of
copying an upstream marker verbatim; so the rule always holds for the text a
stage writes, independent of what an earlier stage deleted. (With resolution
disabled the resolver returns the inliner's text unchanged, which already
satisfies the rule.) Deleting lines can
only remove conditional groups whole or resolve them to one branch; a stage
that keeps an upstream marker emits it again under its own state.

All internal parsers of generated markers (resolver static locations, renderer
model and the rendering invariant's independent re-lex) use one helper,
`PresumedFile`, which applies the same rule when reading: a short marker after
a reset or at the start is an error ("short marker without a preceding marker
in its segment"), and otherwise takes the previous marker's file. Because the
rule makes textual order agree with every path, these textual parsers remain
exact; the rendering invariant still independently checks every token's
file and line. GCC's `-E` output (preprocess check, probe) always names the
file; the parser accepts the short form there too, with standard semantics.
