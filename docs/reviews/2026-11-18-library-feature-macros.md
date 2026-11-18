<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Library feature-test macros: L1c review record

User approval: 2026-11-18, after two adversarial review rounds. This records
the supplied decisions, not a claim that live judges were independently tested.
Part A changes the design and CI pipeline. Resolver implementation is part B;
no `src/` logic changes belong in part A.

The motivating case is `#include <charconv>` followed by
`#if defined(__cpp_lib_to_chars)`. Standard-list includes currently invalidate
reserved feature names, so the condition remains unknown and prevents pruning.
An empty-source compiler dump cannot supply header-defined library macros.

## Rejected round 1: unconditional header snapshots

The first proposal treated each header's feature-macro dump as usable without
tracking source configuration. A user can write
`#define _GLIBCXX_USE_CXX11_ABI 0` before a header; this changes which library
features are supplied and can change their values. An include snapshot taken
under the default configuration then fails to describe the source. The same
problem occurs with reserved `#undef`s, command definitions, macro stacks and
configuration-changing headers/pragmas.

L1c requires a separate default-library-configuration Boolean. It is monotone
along paths and joins with logical AND independently of macro values. A later
matching macro definition cannot restore it. Reserved user writes, extra
reserved command operations, forced includes, nonstandard system headers and
GCC target/optimization option pragmas invalidate it. Macro stacks retain the
existing resolution refusal.

## Rejected round 2: default source configuration alone

The second proposal protected against source changes but trusted a compiler
version's library snapshot on approximate judges. Library **build**
configuration can differ even with identical compiler identity and flags.
MinGW thread configurations (including gthreads), ABI, hostedness, locale and
availability switches can control feature definitions. The snapshot compiler
is not proof of an approximate judge's library build configuration.

Other omissions found in this round were different values for a feature
introduced by different headers, profile/user `-include`/`-imacros`, and header
identity: a user file called `charconv` must not license system-header facts.

L1c therefore audits every actual definition's preprocessor condition stack,
including GCC 13's distributed definitions and GCC 14+'s generated
`bits/version.h`. Parsing `version.def` alone is insufficient. Only
compiler-derived dependencies and verified internal plumbing are admitted.
The two embedded IEEE-format candidate macros require verification of every
installed definition and condition; their names alone are no proof. Unrecognised
and build-configuration dependencies fail closed. Approximate facts additionally
require all matrix builds of the same GCC major to agree on the header's
presence/value. Disagreements are explicit unknowns. Reproduced profiles may
use their own build-dependent snapshot facts, retaining native uncertainty.

Headers accumulate a union of agreeing values; conflicts become unknown.
A9 explicitly records the sequence-of-includes union assumption and must be
reported when relied on. A qualifying spelling must resolve inside the
compiler's own system include directories, not a user directory or a directory
merely designated `-isystem`. Profiles with forced includes get no library facts.

## Part A implementation and limits

`ci/library/` captures per-header dumps, audits textual condition stacks and
transitive request/include dependencies, requires complete matrices, emits
explicit unknowns, compares fresh output to the reviewed baseline, and generates
an independent per-header live diagnostic. The producer versions and raw
condition evidence travel with the data. Candidate-purity checks run against
installed headers in CI; synthetic tests do not assert that an installed GCC
version actually passes them. Unsupported replacements and computed includes
fail closed, potentially reducing useful coverage. CI artifacts must be reviewed
for coverage as well as changes before part B consumes them.

The weekly maintenance workflow uses the existing result and stateful issue
protocol with a separate `library` claim. A passing live probe remains a dated
observation; it never promotes a fact, establishes config independence or
validates A9. The companion uses `DLIB`, leaving the existing DPROBE v1 protocol
and expected outputs intact in this task.

This checkout's assumption list ends at A7; A8 is not defined here. The approved
new assumption retains the user-specified identifier **A9**, without inventing
or renumbering another assumption.

No generated baseline is included in part A's local edits. Run the branch's
`library.yml`, review `library-generated/library.json`, then import it byte for
byte in a follow-up commit on that branch and rerun the PR. Missing data must
fail the comparison; it is never treated as an empty inventory or success.
