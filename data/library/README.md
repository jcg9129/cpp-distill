<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Library header integer macro facts (L1c)

`library.json` is generator-5 CI output stored in schema 3, imported byte for byte after review.
Local compiler measurements or invented baselines must not substitute for that
artifact.

Schema 2/generator 5 extends the approved L1c mechanism from `__cpp_lib_*` to
object-like names in `data/names/standard-macros.txt`. Values must expand through
identifier chains to a single integer literal. Noninteger replacements remain
recorded but unusable; function-like definitions supply no values. Optional
snapshot `function_like` lists record presence only, preventing false absence
claims when another capture supplies an object-like definition of the same name.

The compact file retains per-header snapshots/status, per-major/root independent
literal pairs and dependencies, agreement with explicit unknowns, build metadata
and full-evidence digests. Content-address tables share repeated records. The
separate `library-evidence.json` retains installed definitions, conditions,
compiler inputs, source-entry absence rules, initial-undef proofs and complete
audits. Review that artifact; do not commit it. The A12 member inventory is
unchanged in purpose and remains independent of macro fact selection.

Captures use each profile's own snapshot compiler and exact argv. Approximate
facts require config independence and agreement across every scanned build of
the GCC major. Generator-5 selection additionally requires a compatible captured target and matching ordered
non-harmless flags in one scanned build; target-specific values never come from
a default argv. Reproduced snapshots retain their exact build identity and
native uncertainty. Generators 3/4 remain readable using their feature-only
selection policy, and supply no new standard macro values.

Local satisfaction retains feature-only major agreement under the legacy
coverage rule when its target/ordered argv lacks a matching capture. This cannot
supply standard integer values or absence facts. In the current baseline,
all nine profile captures report `FLT_EVAL_METHOD=0`, but every major's
`cfloat` agreement marks it `config-dependent` with no approved value. Native
dependencies also block AtCoder's own snapshot. These entries remain unknown;
integer observations alone do not override the audit. Enabling this fact for
CSES requires corrected CI audit evidence and a reviewed baseline import.

The resolver applies the [§4.3.3](../../docs/design.md#433-library-feature-test-macro-facts-l1c)
default-library Boolean, compiler-header identity and A9 union/conflict rules.
Relied-on facts report their provenance. DLIB remains a diagnostic, with explicit
unsupported/function-like presence markers that license no facts.

Run the complete [library workflow](../../.github/workflows/library.yml), review
both generated artifacts, then import only `library-generated/library.json` into
this directory. The first comparison fails because the universe and generator
changed; no workflow commits data. Exact commands, review coverage and format
details are in [ci/library/README.md](../../ci/library/README.md).
