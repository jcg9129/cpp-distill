<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# ATL pruning blockers: checkout investigation (2026-11-17)

## Scope and verdict

Investigation only: no source, tests or data changed, no compiler/LLVM SDK build,
and no external ATL checkout or compiler documentation consulted. The reported
44-program run, its exact ATL revision/include order, and v0.1.1's correspondence
to this checkout are **UNVERIFIED**. The ATL snippets below are the question's
inputs; conclusions about distill are traced to this checkout. Proposed compiler
translations are explicitly distinguished from implemented, audited support.

**Yes: with this shipped AtCoder profile, the current pipeline cannot produce a
pruned candidate for any input.** This follows from command handling independently
of ATL: every selected flat configuration includes the full profile flags;
four are unmodelled and seven are untranslated semantic options. Any disable
reason retains every family and prevents pruned rendering. This is not evidence
that no declarations in ATL are removable
(`src/core/validate/configuration.cpp:46`, `src/core/validate/configuration.cpp:58`,
`data/judges/atcoder-gcc15-gnupp23.toml:71`, `src/core/prune/extract.cpp:35`,
`src/core/prune/extract.cpp:71`, `src/core/prune/prune.cpp:196`,
`src/core/pipeline/pipeline.cpp:169`). If flat checks cannot succeed, the outcome
can instead be raw; it still cannot be pruned
(`src/core/pipeline/pipeline.cpp:328`, `src/core/pipeline/pipeline.cpp:338`).

Classification used here: **(a)** implementation/diagnostic defect, **(b)** sound
conservative limitation worth closing with a proof, **(c)** required under the
current contract. Removing a message is not a soundness proof.

| Observed reason | Classification and action |
| --- | --- |
| No-profile `__CHAR_BIT__` differs or unknown | **(c)**. Unknown judge fact, not a failed local probe. Keep the guard; explain it better. |
| No-profile int128/MSVC conditions unknown | **(c)**. Deliberately no judge compiler/platform facts. Documentation/UX only within this design. |
| AtCoder wide-arithmetic Boolean condition unknown | **(b)** where a known term could settle a valid Boolean expression but the closure contains unknown facts. Not an implementation violation of the documented evaluator. Exact ATL trigger remains **UNVERIFIED**; a concrete header-invalidation path is established below. |
| `intrin.h` still present | **(c)** while its branch is unresolved or live. It is not an audited standard header. Resolve the enclosing condition safely; do not globally whitelist the header. |
| Four profile-inert flags reported as unmodelled | **(a)** loss of profile/preprocessing-exemption context in the pruning permission diagnostic; the same flags were admitted by the loader. A fix must preserve the independent semantic gate. |
| Those four also reported as untranslated semantic options | **(b)** pending separate translation or proof that omission preserves pruning dependencies. Profile preprocessing inertness does not supply that proof. |
| `-fmodules`, `-fopenmp`, `-march=native` untranslated | **(b)** potential support work; **(c)** to retain the disables until their specific obligations are met. No blanket inert exemption. |
| Standard `[[no_unique_address]]` on ordinary data members | **(b)** a narrow, useful attribute-policy extension, with whole-record retention. |

The gates behind these classifications are explicit: unknown condition,
non-standard header and unequal/unknown predefined macro values disable at
`src/core/prune/prune.cpp:149`, `src/core/prune/prune.cpp:161` and
`src/core/prune/prune.cpp:165`; unsupported attributes disable at
`src/clang/prune_extract.cpp:566`. Existing tests deliberately accumulate reasons
and require unknown predefined values to disable
(`tests/unit/prune_test.cpp:133`, `tests/unit/prune_test.cpp:140`,
`tests/runner/prune_extract_tests.cpp:266`).

## 1. Why local probes do not establish judge facts

This is the intended design, not a missing propagation fix. The local machine is
explicitly not the judge (`docs/design.md:34`). Without a profile reserved names
start unknown; non-reserved source-defined names start undefined under A3, and
other non-reserved names are unknown too (`docs/design.md:578`). The design
specifically says that neither local predefined macros nor the local integer
model enter initial resolution; only the satisfaction rerun uses observed local
facts (`docs/design.md:734`).

The implementation passes only the local effective dialect when no profile is
selected (`src/core/pipeline/pipeline.cpp:259`). Resolver initial state consults
source definitions, the profile if present, then the dialect exception for
`__cplusplus`; everything else is unknown (`src/core/resolve/resolve.cpp:52`).
Thus local GCC knowing `__CHAR_BIT__`, `__SIZEOF_INT128__`, and the absence of
`_MSC_VER` does not make any of them known judge facts. The probe still serves
configuration selection, parsing and verification: `probeConfiguration` builds
observed facts (`src/core/validate/configuration.cpp:12`), and `satisfies` reruns
resolution against those facts (`src/core/validate/configuration.cpp:67`). It
does not replace the original judge requirements.

For value expansions, pruning receives only a no-profile `__cplusplus` exception
in its judge initial map (`src/core/prune/extract.cpp:36`). Clang callbacks record
user expansions, classify compiler predefinitions, and leave missing no-profile
judge definitions unknown (`src/clang/prune_extract.cpp:339`,
`src/clang/prune_extract.cpp:357`). Equality requires both definitions known and
token-equivalent (`src/core/prune/prune.cpp:22`); hence the `__CHAR_BIT__` refusal
is correct even if both local GCC and Clang happen to use 8.

For the quoted config expression, `defined(__SIZEOF_INT128__)` alone encounters
an unknown reserved fact without a profile. Whether `ATL_NO_INT128` and
`ATL_FORCE_FALLBACKS` start undefined depends on whether source defines them
anywhere (A3), not merely on their absence from the compile command
(`src/core/resolve/resolve.cpp:53`, `docs/design.md:586`). Do not infer they are
false without that inventory. With no profile this is classification **(c)**;
changing to local-compiler facts would change the product contract. A separate,
explicit local-environment mode would need a design decision.

## 2. AtCoder facts, headers and the Boolean expression

The current shipped profile is reproduced, not merely approximate: its snapshot
says so, its build provenance matches, and the shipped-profile unit test asserts
it (`data/judges/atcoder-gcc15-gnupp23-predefined.txt:8`,
`src/core/profile/facts.cpp:185`, `tests/unit/profile_test.cpp:149`). Its initial
ordinary facts include `__CHAR_BIT__ = 8` and `__SIZEOF_INT128__ = 16`
(`data/judges/atcoder-gcc15-gnupp23-predefined.txt:133`,
`data/judges/atcoder-gcc15-gnupp23-predefined.txt:494`). The snapshot contains no
definition/state entry for `_MSC_VER`, `_M_X64` or `_M_ARM64`; reproduced-profile
absence makes them initially undefined by A4 (`src/core/profile/facts.cpp:63`,
`src/core/profile/facts.cpp:81`). These particular names are not unknown merely
because of `-march=native`.

There are two separate mechanisms to check:

1. **A header can destroy negative reserved facts.** All names occurring in the
   input are inventoried and initialized (`src/core/resolve/resolve.cpp:76`,
   `src/core/resolve/resolve.cpp:381`). At a standard include, only positively
   known compiler/special/native predefined facts get builtin protection.
   `_MSC_VER` initially undefined by A4 does not qualify. Reserved names also
   fail `protectedUserMacro`, so an include such as `<cstdint>` makes the three
   absent MSVC names unknown (`src/core/resolve/resolve.cpp:261`,
   `src/core/names/names.cpp:57`, `src/core/names/names.cpp:98`). In contrast,
   the known compiler fact `__SIZEOF_INT128__` survives. A non-standard system
   include invalidates every macro, including source-derived `ATL_HAS_INT128`
   (`src/core/resolve/resolve.cpp:271`). Unknown alternatives can also join
   different definitions into unknown (`src/core/resolve/resolve.cpp:279`,
   `src/core/resolve/resolve.cpp:308`).
2. **The resolver does not simplify Boolean expressions with unknown leaves.**
   It scans the whole macro closure and immediately refuses an unknown fact,
   including a `defined` operand (`src/core/resolve/resolve.cpp:174`,
   `src/core/resolve/resolve.cpp:184`, `src/core/resolve/resolve.cpp:198`). It
   never reaches the evaluator in that case
   (`src/core/resolve/resolve.cpp:213`, `src/core/resolve/resolve.cpp:226`).
   This implements the documented whole-closure policy
   (`docs/design.md:632`), including conservative inspection of unused macro
   arguments (`tests/unit/resolve_test.cpp:75`).

Consequently, even with `ATL_HAS_INT128` known 1, a preceding standard header
can make the later `defined(_MSC_VER)` unknown and stop evaluation. For
`(I || (M && (X || A))) && !P && !F`, **I = 1 alone is insufficient**: P and F
must also be known false to conclude true. With those facts established, the
unknown M/X/A leaves cannot change the mathematical Boolean result, but today's
closure gate still refuses it. Likewise M known false does not currently prevent
the closure walk from rejecting unknown X/A in `M && (X || A)`.

This is a sound precision gap **(b)**, not a bug in Clang's short-circuit
evaluation. A bounded Boolean proof over validated, safely expanded expressions
could improve it. Merely substituting 0 for unknown names is unsound; arbitrary
macro replacements can introduce operators, malformed tokens, pasting or active
builtins. A new evaluator must retain those barriers and record only justified
external requirements, so satisfaction reruns remain meaningful
(`docs/design.md:637`, `src/core/resolve/resolve.cpp:177`,
`src/core/resolve/resolve.cpp:207`, `src/core/validate/configuration.cpp:67`).

The exact prior header and first unknown leaf in the reported ATL run are
**UNVERIFIED**, because ATL is not in this checkout. If all leaves remain known
and no invalidation occurs, these supplied expressions should resolve with the
shipped profile; there is no code-supported reason to claim wide_arithmetic:8
is intrinsically unknown. The documented A1/A2 assumptions permit headers to
define reserved names and only protect names actually predefined by the compiler
(`docs/design.md:90`). Preserving every absent reserved fact across headers
would therefore be unsound under today's contract. That invalidation is **(c)**,
even though a Boolean precision improvement can avoid depending on the fact.

`intrin.h` has a further cascade effect. Unknown groups keep all possible paths
and join their states (`src/core/resolve/resolve.cpp:308`); pruning preparation
collects includes still present in the flat text without evaluating their guards
(`src/core/prune/extract.cpp:100`). A surviving `intrin.h` therefore disables even
if local Clang skips it (`src/core/prune/prune.cpp:161`). The original unknown
flag is authoritative; taking one Clang branch never clears it
(`docs/design.md:780`). Resolving the dead MSVC branch removes the include;
otherwise the header guard must remain. Also, the disable location is only the
first remaining opening conditional found in flat text, not a stored exact
unknown-decision location (`src/core/prune/prune.cpp:150`). Improve that diagnostic
before treating the reported line as a precise root cause.

## 3. The two option gates and verification

The complete path is:

1. The profile loader reads `flags` and `inert`, parses flags using the command
   classifier, admits an unmodelled flag only if its exact spelling is inert,
   and requires each inert entry to name an unmodelled flag actually present
   (`src/core/profile/profile.cpp:131`, `src/core/profile/profile.cpp:151`,
   `src/core/profile/profile.cpp:162`, `src/core/profile/profile.cpp:164`).
   This does not change its `Arg.classification`. The four spellings are listed
   as preprocessing-inert in the AtCoder file
   (`data/judges/atcoder-gcc15-gnupp23.toml:81`). This meaning agrees with
   `docs/design.md:279`, and profile-loader tests enforce it
   (`tests/unit/profile_test.cpp:529`).
2. Initial resolution gets the original local command and the selected profile
   (`src/core/pipeline/pipeline.cpp:259`); the reported ordinary local command
   has none of the profile-only unmodelled flags. Later `flatCommand` removes
   local macro operations/dialect, appends profile std and full flags, and
   reparses, with no inert context (`src/core/validate/configuration.cpp:49`,
   `src/core/validate/configuration.cpp:56`). The four remain unmodelled: no
   classifier branch recognizes them (`src/core/command/command.cpp:102`,
   `src/core/command/command.cpp:133`, `src/core/command/command.cpp:144`).
3. `preparePruning` imports `c.command.pruningAllowed()` reasons
   (`src/core/prune/extract.cpp:35`). This delegates to `resolutionAllowed`,
   which unconditionally reports every unmodelled argument
   (`src/core/command/command.cpp:191`). Neither call sees `Profile::inert`.
   Hence the first four disables, even for `int main(){}`. This is the profile
   context/diagnostic defect **(a)**; a preprocessing exemption needs explicit
   provenance rather than globally accepting the same spelling in arbitrary
   user commands.
4. A second, independent pass translates supported options into **cc1** arguments.
   Anything else is recorded in `untranslatedOptions`. Its local `inert` Boolean
   means only `Class::harmless` except `-Ofast`; it is unrelated to the profile
   list (`src/core/prune/extract.cpp:71`, `src/core/prune/extract.cpp:86`).
   Thus all four unmodelled flags and the modelled `-fmodules`, `-fopenmp` and
   `-march=native` disable again. Those last three are deliberately modelled
   for command/preprocessing purposes (`src/core/command/command.cpp:133`,
   `src/core/command/command.cpp:145`); modelled does not guarantee an implemented
   pruning translation. The translation table tests explicitly expect their
   refusals (`tests/unit/prune_test.cpp:231`).

**Preprocessing inertness and safe omission from an AST dependency parse are
different guarantees.** Reusing `judge->inert` for the second gate would silently
assert the stronger guarantee. Even a fix to gate 3 must leave gate 4 intact
until there is a per-option proof, matching `docs/design.md:827`.

| Profile option | Proposed disposition; what is and is not established here |
| --- | --- |
| `-fconstexpr-depth=1024` | Candidate translation to `-fconstexpr-depth=1024`; do not map to a step/loop limit. The profile documents it only as preprocessing-inert. Exact pinned LLVM cc1 acceptance and semantic-limit behavior are **UNVERIFIED** from this checkout, which has no such translation. Add explicit translation and acceptance/limit tests before enabling. |
| `-fconstexpr-loop-limit=524288` | GCC loop-resource policy. No implemented Clang equivalent here; a claim of an exact compiler equivalent is **UNVERIFIED**. Do not rename to depth or assume Clang's step budget counts GCC loops. Candidate parse-only omission needs an argument that successful parsing cannot lose retention dependencies; resource-induced parse errors must still disable. |
| `-fconstexpr-ops-limit=2097152` | Same obligation, with operation budget rather than loop count. A proposed Clang `-fconstexpr-steps` setting is not established as an equivalent GCC operation budget. The reviewer's claim that these GCC options have no direct Clang equivalent is plausible but **UNVERIFIED** without compiler sources; there is certainly no translation or equivalence proof in this code. |
| `-ftrivial-auto-var-init=zero` | Candidate cc1 translation preserving zero initialization, or a narrowly audited omission for dependency extraction. It is not runtime-inert; retain it in checking/build commands. Exact LLVM 23 cc1 support and the absence of extra lookup/instantiation effects are **UNVERIFIED** here. Keep disabling until audited. |
| `-fopenmp` | Do not omit just because it is modelled. `_OPENMP` effects are tested (`tests/unit/option_model_test.cpp:132`); pragma parsing needs a separate policy. The core currently rejects `#pragma omp` because only its listed pragma names are supported (`src/core/prune/extract.cpp:11`). Support needs a cc1 OpenMP mode/version audit, macro checks and dependency traversal for directives/clauses, or a proven restricted input subset with conservative refusal elsewhere. Merely injecting `_OPENMP` cannot establish OpenMP semantics. Exact GCC/LLVM parity is **UNVERIFIED**. |
| `-fmodules` | GCC C++20 modules cannot be assumed to mean Clang's similarly spelled header-module option. `__cpp_modules` effects are tested (`tests/unit/option_model_test.cpp:133`). Module/import syntax and imported declarations require an explicit policy; this checkout supplies no translation. A restricted non-module input omission is conceivable only after auditing tokens (including macro-produced ones), predefines and AST forms. A same-spelling forwarding fix is unsupported; exact pinned frontend mapping is **UNVERIFIED**. |
| `-march=native` | Keep disabling in the current implementation. The audited native model bounds uncertain preprocessing facts, not a cc1 CPU/feature vector. Driver options must become target-specific `-target-cpu`/features, not be forwarded verbatim (`src/core/README.md:661`). A snapshot from one host cannot fix all judge hosts' features (`docs/design.md:336`, `data/native/gcc-15.2-x86.txt:7`). Macro-value checks alone do not prove lookup/builtin/type support unchanged. Any restricted omission or translation needs a separate argument covering these effects and must preserve native unknown facts. |

Existing forwarding of `-O2`, ordered `-D`/`-U`, `-pthread`, character options and
exception/RTTI defaults demonstrates the intended explicit translation approach
(`src/core/README.md:650`, `src/core/README.md:655`,
`tests/unit/prune_test.cpp:203`). These do not establish support for the seven
flags above.

**There is another unconditional blocker on check-compiler paths:** profile link
options are appended as `Class::libraries` (`src/core/validate/configuration.cpp:63`),
and `pruningAllowed` rejects every such argument
(`src/core/command/command.cpp:198`). AtCoder's substantial link list includes
external libraries (`data/judges/atcoder-gcc15-gnupp23.toml:83`). A local fallback
does not append that list; a compatible selected check compiler does. Fixing the
seven compile flags alone therefore does not guarantee the profile can prune
on either path. The external-library refusal is **(c)** under
`docs/design.md:79`: external code can call into user declarations. Do not
silently remove judge link options to enable pruning; distinguishing linker
policy from actual external libraries is a separate bounded improvement.

**Target verification does not override any of these gates.** Unavailable profile
check compilers are reported, and local fallback is still attempted with full
profile flags (`src/core/pipeline/pipeline.cpp:276`). An available check compiler
with an incompatible triple is skipped (`src/core/pipeline/pipeline.cpp:287`).
No satisfying configuration or a failed flat check can force raw; local fallback
is not guaranteed to succeed (`src/core/pipeline/pipeline.cpp:297`,
`src/core/pipeline/pipeline.cpp:335`). Which compiler is actually available in
the reported run is **UNVERIFIED**.

If flat succeeds, its selected configuration feeds pruning irrespective of the
`local`/`target` label (`src/core/pipeline/pipeline.cpp:152`,
`src/core/pipeline/pipeline.cpp:327`, `src/core/pipeline/pipeline.cpp:338`). The
profile's judge facts do not become weaker merely because its compiler is
missing locally. Moreover this shipped profile's `-march=native` makes
`targetEquivalent` false **even with an available otherwise matching compiler**:
host-dependent options are rejected before identity/macro comparisons
(`src/core/validate/configuration.cpp:7`, `src/core/validate/configuration.cpp:77`).
The design explicitly separates checking verification from parsing semantics
(`docs/design.md:827`). `--strict` requires both pruned and target for exit 0;
that is a final result rule, not a pruning permission
(`src/core/pipeline/pipeline.cpp:341`).

## 4. Minimal safe support for no_unique_address

The present allowlist contains some layout attributes but no no-unique-address
attribute (`src/clang/prune_extract.cpp:33`). `VisitDecl` inspects members as well
as top-level declarations, so a supported-looking field still disables its
whole analysis (`src/clang/prune_extract.cpp:553`,
`src/clang/prune_extract.cpp:560`). The refusal is a positive-policy gap, not
evidence that fields are pruned independently.

Records own complete lexical contents by design (`docs/design.md:850`). Ownership
walks declaration contexts to the containing record, and out-of-line member
specializations are united with it (`src/clang/prune_extract.cpp:95`,
`src/clang/prune_extract.cpp:278`). The code explicitly relies on never pruning
individual members (`src/clang/prune_extract.cpp:605`). Thus retaining a record
retains the attributed field, its spelling, order and other fields; deleting an
unneeded whole record deletes it with its attribute. The attribute itself need
not root every otherwise-unused record.

The narrow proposed rule is: accept the standard, **argument-free** spelling only
on a **non-static, non-bit-field data member**, with verified ownership by the
whole record. Validate declaration context and parsed attribute kind, rather
than adding a bare spelling to a context-free allowlist. Preserve invalid-AST
and parse-error rejection (`src/clang/prune_extract.cpp:561`,
`src/clang/prune_extract.cpp:682`). Exact Clang attribute class/spelling behavior,
including whether invalid arguments are discarded as warnings, is
**UNVERIFIED** here; the implementation must test the pinned frontend and reject
unsupported spellings/arguments rather than relying on a missing AST attribute.

This requires no new field-liveness or layout evaluator. It still requires all
ordinary dependencies of the member's type, initialization, constructors and
destructors: the existing visitors traverse type locations, references,
construction and destruction (`src/clang/prune_extract.cpp:590`). Attribute
support must not bypass that traversal. The soundness argument is preservation
of each retained record's complete source and dependency closure, not equality
of Clang and GCC layouts or `sizeof` results. The design explicitly does not
assume cross-compiler layout/overload equality (`docs/design.md:815`). Tests must
cover lookup/trait/template paths whose choice can differ with layout, so support
does not accidentally depend on one observed layout branch. The existing
field-layout fixture already illustrates why an unreferenced field must stay
(`tests/corpus/caide/field-layout-sizeof/1.cpp:1`).

Treat `[[msvc::no_unique_address]]` as a **separate rule**, initially unsupported.
MSVC judge compilation is a non-goal (`docs/design.md:45`), but source using a
vendor spelling can still be compiled by supported judges. Whether GCC ignores
it and Clang models it, and its accepted contexts/ABI behavior, are
**UNVERIFIED** from this checkout. Never silently equate the two spellings by
name alone. A later spelling-specific audit can permit the same restricted field
contexts if unchanged source, dependencies and frontend handling justify it.

## Verification performed

Read the requested design, option table, profile, implementation and existing
unit/case fixtures. Checked the shipped TOML/snapshot facts, the complete flag
route, both disable gates, header invalidation, closure rejection, ownership and
verification control flow. A read-only report check validates cited paths/line
bounds and the report's SPDX header. No executable tests or builds were run;
this investigation supplies proposed regressions below, not claims that fixes
already pass them. Only this report is added.

## Ordered PR plan

1. **Make the blockers and their provenance explicit.** Preserve the exact unknown
   resolver decision/location in the pruning diagnostic, distinguish unknown
   judge facts from differing known values, and explain that profile `inert` is
   preprocessing-only. Remove the misleading unmodelled permission reason for
   an exact profile-origin inert occurrence while retaining its semantic disable
   and refusing untrusted local occurrences. Add tests in
   `tests/unit/prune_test.cpp` and `tests/unit/validate_test.cpp` for the actual
   shipped profile through `flatCommand` into `preparePruning`, plus a
   `tests/cases/pipeline/` fixture with multiple retained conditionals proving
   the correct location is reported. Test all seven flags and check/local link
   differences, not only loader acceptance. Amend `docs/design.md:217` (modelled
   does not always mean translated; include the implemented module/OpenMP/CPU
   cases), `docs/design.md:279` and `docs/design.md:827` to name the two distinct
   guarantees and explain no-profile behavior without changing its contract.
2. **Add narrowly proved parse-option support in separate small PRs.** Start with
   constexpr depth, then separately loop/operation resource-limit handling and
   trivial-auto-init translation/omission. Verify pinned compiler sources first;
   do not infer flags from driver acceptance. Extend the complete cc1 argv table
   in `tests/unit/prune_test.cpp`, worker extraction tests in
   `tests/runner/prune_extract_tests.cpp`, and `tests/cases/prune/` fixtures for
   successful constant evaluation, limits causing refusal, template constraints
   and dependency retention. Test every accepted exact spelling, malformed values
   and unchanged build flags. Amend `docs/design.md:827`, the README translation
   table and the versioned policy for each proved omission. These PRs remove
   individual blockers; the profile remains disabled by the other flags/linkage.
3. **Support standard no_unique_address on owned fields.** Apply the narrow rule
   above; postpone the vendor spelling. Add worker tests for kept/removed whole
   records, templates/nested records, empty/nonempty members, constructor and
   destructor dependencies, shared declarators and macro-produced attributes.
   Add `tests/cases/prune/` and `tests/cases/pipeline/` layout/address/sizeof/trait
   fixtures that remove an unrelated function while preserving runtime behavior
   and field spelling; negative tests cover arguments, static members, bit-fields
   and other declaration contexts. Keep unknown-attribute and error-recovery
   refusals. Document supported contexts at `docs/design.md:980` and update
   `src/core/prune/prune.h:10` policy version when shipping support.
4. **Improve Boolean precision with a bounded proof.** Implement only a validated
   subset that can prove a result independent of unknown leaves; preserve macro
   expansion hazards and target arithmetic barriers. Add
   `tests/unit/resolve_test.cpp` truth tables for the quoted expression with I/P/F
   combinations, false-left conjunctions, unknown switch facts, malformed/pasting/
   builtin replacements and satisfaction reruns. Add `tests/cases/resolve/` and
   `tests/cases/pipeline/` fixtures with a standard include before absent MSVC
   names and a conditional `intrin.h`, plus non-standard-header invalidation and
   differing branch joins. Require the no-profile unknown-fact tests to remain
   correct. Amend the normative whole-closure algorithm at
   `docs/design.md:632`; do not weaken A1/A2 to preserve absent reserved names.
5. **Audit optional language modes separately.** One PR for OpenMP mode/macros
   and directive/clauses, another for a restricted modules policy or actual
   module support. Add cc1 table and worker tests, `_OPENMP`/`__cpp_modules`
   value/conditional fixtures, user calls and types in directives, and explicit
   `module`/`import` and unsupported-form refusal fixtures under `tests/cases/`.
   Keep a blanket disable until that PR proves either faithful translation or
   a checked input restriction. Amend `docs/design.md:827`,
   `docs/design.md:974` and the supported-AST policy as appropriate. Never enable
   these by classifying the flags harmless globally.
6. **Address native targets and external linkage as separate proof projects.**
   For native CPU options, audit driver-to-cc1 feature handling or justify a
   restricted omission; add host/target mismatch tests, feature-macro/builtin/type
   fixtures, and regressions proving native facts stay unknown and target level
   stays unavailable. For linkage, distinguish actual callback-capable libraries
   from linker policy only after an audit; add check-compiler versus local-fallback
   pipeline tests with AtCoder's link list and an external callback counterexample.
   Keep disabling unaudited libraries; any wider enablement needs explicit changes
   to `docs/design.md:79`, `docs/design.md:336`, `docs/design.md:755` and
   `docs/design.md:827` as applicable. End with a shipped-profile end-to-end test
   that either removes a simple unused declaration with every proof in place or
   asserts the remaining named blocker; do not promise ATL pruning merely because
   earlier PRs reduced the diagnostic count.
