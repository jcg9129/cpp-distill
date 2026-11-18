<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Approximate GCC allow-list review (2026-11-18)

## Verdict

Proceed with rule 1 as an **absence-only**, initial-environment rule for
GCC 13–16 snapshots. Proceed with rule 3 only with the additional checks below.
The proposed exact-width-type-only derivation is insufficient: GCC uses the
least-width types, and unsigned narrow constants depend on integer promotions.
Neither rule establishes facts without a snapshot, promotes a profile to
reproduced, nor authenticates an inferred judge target.

These are amendments to the approximate-profile policy under A4, approved in
direction by the user on 2026-11-18. They remain conditional on the profile
actually describing GCC and its command and ABI. A3 and A5 are unchanged.

## Source audit and pins

Streamed the public GCC archives and searched identifier-boundary occurrences
of all 18 proposed identity names in every regular file below `gcc/` and
`libcpp/`, excluding testsuites. This includes all target configs, driver
specs, C-family builtins and preprocessor sources, rather than just a Linux
`-dM` sample. Pins (the GCC 16 development branch was resolved before scanning):

| Source | Commit | Files scanned | Matching lines |
| --- | --- | ---: | ---: |
| [GCC 13.4.0](https://github.com/gcc-mirror/gcc/tree/99677969d463d75a562f94460ea75e9f6a016b4f) | `99677969d463d75a562f94460ea75e9f6a016b4f` | 7436 | 10 |
| [GCC 14.3.0](https://github.com/gcc-mirror/gcc/tree/c9cd41fba9ebd288c4f101e4b99da934bcb96a11) | `c9cd41fba9ebd288c4f101e4b99da934bcb96a11` | 7850 | 22 |
| [GCC 15.2.0](https://github.com/gcc-mirror/gcc/tree/5115c7e447fc07457443df874bf57840e8316d5f) | `5115c7e447fc07457443df874bf57840e8316d5f` | 8233 | 22 |
| [GCC 16 branch](https://github.com/gcc-mirror/gcc/tree/c4b0d6df609061b67f94e2bf27ee8d154b4b5610) | `c4b0d6df609061b67f94e2bf27ee8d154b4b5610` | 8511 | 33 |

All hits consume host-compiler identities (for example `gcc/system.h`, Rust
utility headers, D diagnostics, configure probes and `libcpp/line-map.cc`).
None defines these identifiers or passes them to a target builtin-definition
hook or spec. In particular there are no hits in `gcc/config/`, including
`i386/mingw32.h` and `i386/mingw-w64.h`. This is evidence for the audited
upstream revisions, not every historical GCC revision, downstream patch,
plugin, wrapper or future release. Bound the new rules to GCC majors 13–16;
review the sources again before extending them.

## Rule 1: attacks and limits

Admit initial absence of exactly:

```
_MSC_VER _MSC_FULL_VER _MSC_BUILD
_M_X64 _M_AMD64 _M_IX86 _M_ARM _M_ARM64 _M_ARM64EC
__clang__ __clang_major__ __clang_minor__ __clang_patchlevel__
__INTEL_COMPILER __INTEL_LLVM_COMPILER __NVCC__ __CUDACC__ __EMSCRIPTEN__
```

Do not admit an unexpected positive snapshot definition merely because its
name appears here. GCC identity is already a profile assertion, not something
proved by `__GNUC__` alone: Clang and Intel also emulate GNU version macros.
Vendor specs, wrappers and downstream builds can supply `-D__clang__`; the
[GCC libstdc++ change documenting precisely that case](https://gcc.gnu.org/git/?p=gcc.git;a=commit;h=67be156f952241ad99af1eedc4da91bc2e201cd3)
is a concrete counterexample to an unconditional claim about every GCC command.
Explicit profile command operations still win, including positive `-D` and
negative `-U`; unrecorded judge operations violate A4's command assertion.
Local compilation flags cannot become judge facts. Source writes follow the
existing state transitions, joins and invalidation rules.

This is initial absence, not a promise that headers never define these names.
The [A2 identity audit](2026-11-18-a2-identity-macros.md) found MinGW-w64's
`_mingw_mac.h` synthesizes all six listed `_M_*` aliases. Keep its exclusions:
Codeforces and Timus may start with these aliases absent but must lose that
knowledge across standard headers. `_MSC_VER` is covered for MinGW-w64;
the whole MSVC conjunction need not resolve there because the resolver checks
every dependency rather than short-circuiting unknown operands. Linux/glibc
profiles retain all three guard facts under the existing A2 exemption.
Unknown or unregistered libraries must keep A2's current conservative behavior.

## Rule 3: attacks and admissible derivation

Pinned implementation: GCC 15.2.0
[`c-family/c-cppbuiltin.cc`, `builtin_define_stdint_macros`, `type_suffix`,
`builtin_define_constants`](https://github.com/gcc-mirror/gcc/blob/5115c7e447fc07457443df874bf57840e8316d5f/gcc/c-family/c-cppbuiltin.cc#L439).
The corresponding routines in the other three pins implement the same rule.
The width-specific constant helpers use `int_leastN_type_node` and
`uint_leastN_type_node`; the MAX helpers use `intmax_type_node` and
`uintmax_type_node`. Suffix selection applies integer promotions, then emits
a one-parameter function with either its parameter alone or a paste with the
suffix. It does not use the exact-width nodes for the constant helpers.

Implement the following deliberately conservative subset:

1. Recognize only the ten helpers `__INT8_C`, `__INT16_C`, `__INT32_C`,
   `__INT64_C`, their unsigned counterparts, `__INTMAX_C` and `__UINTMAX_C`.
2. Require a known ordinary compiler-builtin definition. Its corresponding
   allow-listed `__INTn_TYPE__`/`__UINTn_TYPE__` fact must be a known object
   definition from this same snapshot, with compiler-builtin provenance.
   For width-specific helpers also require the corresponding `*_LEASTn_TYPE__`
   definition to agree. A different least type may be valid GCC, but is outside
   this subset. MAX needs only its own type fact.
3. Accept only canonical standard signed/unsigned char, short, int, long and
   long long type spellings. Decline plain char, extended integers and unknown
   spellings. For unsigned char/short, compare the corresponding allow-listed
   `__SCHAR_WIDTH__`/`__SHRT_WIDTH__` with `__INT_WIDTH__`: narrower promotes
   to int (no suffix), equal width requires `U`. Missing, malformed or
   command-overridden prerequisites leave the helper unknown.
4. Require exactly GCC's function shape, parameter `c`, no variadic arguments,
   and replacement `c` or `c ## SUFFIX`, where the suffix is one of `U`, `L`,
   `UL`, `LL`, `ULL`. Refuse inconsistent suffixes, extra tokens, wrong arity
   or object definitions. Conservative spelling rejection is acceptable.
5. Only admit positive definitions that pass. Missing helpers and explicit
   non-command unknown/undefined states remain unknown. Never synthesize a
   replacement, edit `snapshotFacts`/`predefined`, or hide the original bytes
   from diagnostics. Reproduced snapshots retain their existing policy.

LP64 QOJ/CSES use `__UINT64_C(c) c ## UL`; LLP64 Codeforces/Timus use
`c ## ULL`. Trusting a host suffix or selecting from the numeric width alone
would silently break one ABI. A forged LP64 snapshot with `ULL` must fail the
check even though both tokens denote 64-bit unsigned values. Unsigned short on
a 16-bit-int target is another counterexample to blindly omitting `U`.

Profile `-D`/`-U` for the helper itself remains authoritative. Overriding a
type/width macro, however, does not change GCC's internal type node; it cannot
justify a different helper. Reject those prerequisites for derivation, even
when their replacement looks plausible. No-snapshot profiles cannot derive
the helpers. Header preservation uses the existing A2 compiler-builtin rule;
this does not permit resolving arbitrary source token-paste conditions.

## Inference and detection gaps

CSES infers OS/architecture from language versions; QOJ infers the triple from
published OS/CPU; Timus and Codeforces approximate particular MinGW packages.
The derivation proves internal consistency conditional on these already
trusted type facts. It does not independently establish them. An internally
consistent but wrong ABI passes, just as other allow-listed type facts do.
Keep those profile caveats, snapshot compiler provenance and approximate label.

The drift reproduction check compares snapshot and expected-probe bytes, so
an altered helper or type remains visible. Evidence/availability monitoring
does not prove judge deployment or detect every hidden spec or wrapper.
`--judge-check` checks selected presence facts, object expansions and library
diagnostics. Its current fixed extra-name set includes `__clang__`, but most
new absent identity names are not selected unless present in the snapshot.
Thus newly judge-defined `_MSC_VER` can escape that check. Constant helpers
are function-like and presence-only: a changed body with unchanged types can
also escape. Type expansions can expose ABI drift, but cannot certify the
helper's definition. A local preprocessing/parse check detects only mismatches
in configurations actually checked. None of these observations changes facts
automatically or closes A4's assumptions; do not claim new detection coverage.

## Phase 2 validation contract

Test shipped approximate QOJ, CSES and Codeforces startup absence and all ten
consistent helpers; forge inconsistent definitions and prerequisites to check
that they stay unknown. Exercise ordered command overrides and MinGW's A2
restriction. Use the LLVM-free pipeline's existing synthetic extraction worker
with a shipped Linux profile for `<cstdint>`, the MSVC `<intrin.h>` guard and
`UINT64_C`, and verify removal of an unrelated function. The synthetic model
must explicitly report the predefined constant expansion to exercise the
pruner's unknown-judge-fact check; real Clang extraction remains CI coverage.
