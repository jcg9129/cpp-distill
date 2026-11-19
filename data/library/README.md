<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Library feature-test macro facts (L1c)

`library.json` belongs here only after generation by branch CI and human review.
It is deliberately absent from the initial part A change: no local compiler
measurements or invented baseline may substitute for CI artifacts. The first
comparison fails and uploads the candidate for the normal follow-up import.

The compact schema/generator 2 baseline includes per-header snapshots/status,
per-GCC-major/root independent pairs and dependencies, and cross-build agreement
with explicit unknowns. Each build records a SHA-256 of its full evidence. The
separate uploaded `library-evidence.json` retains complete audits for review; do
not commit it. Repeated baseline records use deterministic content-address tables.
Producer scripts, exact capture/import commands,
coverage limitations and diagnostic probes are documented in
[ci/library/README.md](../../ci/library/README.md). Data is regenerated on relevant
PRs and weekly through profile maintenance; differences fail with a readable diff
and drift issues use the existing stateful reporting protocol. Part B will add
resolver consumption; these files do not change today's resolver behavior.
