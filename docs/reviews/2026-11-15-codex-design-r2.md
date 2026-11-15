**I would not approve v2 as written.** It resolves several structural problems from R1, but still permits silent changes in judge behavior. The main blockers are the macro resolver’s assumptions, observable file boundaries, and pruning decisions derived from one local AST.

I assume equivalence means comparing the original source tree and the generated file **under the same judge configuration**. Local and judge executions need not agree with each other. I also assume defined program behavior; nondeterministic programs need a separate policy for runtime comparisons. The counterexamples below are reasoned examples, not results from running an implementation.

“Resolved” below means resolved at the design level, not validated by implementation or release measurements.

| R1 | Status | Assessment |
|---|---|---|
| 1 | Partly | Identifier matching preserves the original named-overload example. Implicit operations and generic operators in unparsed branches remain uncovered. |
| 2 | Partly | Transitive expansion and control dependence are acknowledged. Header effects, macro stacks, expansion side effects, and unknown replacement lists remain unsound or unspecified. |
| 3 | Partly | Textual traversal reaches includes in skipped branches. Header availability tests, recursive inclusion, and unavailable alternative headers still need a defined algorithm. |
| 4 | Resolved | Keeping system includes in place and retaining repeated occurrences addresses the hoisting/deduplication objection. |
| 5 | Partly | Repeated textual inclusion is appropriate, but the stated duplicate-elimination predicate does not prove that the guard is currently defined. |
| 6 | Resolved | One primary translation unit is now explicit. |
| 7 | Partly | Rejecting include-depth macros and inhibiting pruning for `__COUNTER__` helps. Resolution can also consume counters; splice-only line markers do not survive later deletions. |
| 8 | Resolved | Keeping reachable records whole addresses the member-removal examples. |
| 9 | Partly | Whole overload sets and global name matching help substantially. Generic implicit operations and dependencies through system code still escape the rules. |
| 10 | Partly | Template families and redeclarations are much clearer. Overlapping families, multi-range ownership, and references to noneditable system primaries still need specification. |
| 11 | Partly | The root list is improved. Root classification can itself depend on the target, and traversal through retained system code is not defined. |
| 12 | Partly | Keeping macro directives resolves the macro-deletion problem. The new resolver still needs macro-event semantics, especially `push_macro`/`pop_macro` and `_Pragma`. |
| 13 | Partly | Immutable text and protected-token checks are good. Family ownership, final source-location preservation, and comment stripping remain incomplete. |
| 14 | Partly | Structured arguments are the right foundation. Sending arbitrary unmodelled semantic options only to verification is insufficient. |
| 15 | Partly | The compatibility matrix and parse-failure fallback help. Successful parsing still does not establish target-independent pruning decisions. |
| 16 | Partly | The original is now the baseline. However, profile-resolved output is compared against an original built under potentially different macro settings. |
| 17 | Resolved | Removing repair from v1 removes the disputed repair argument. |
| 18 | Partly | Workers are appropriate, but process-tree termination, early pipeline stages, resource limits, and fallback verification remain unspecified. |
| 19 | Resolved | Separating retention, equivalence, and checker tests addresses the testing objection. |
| 20 | Resolved | D1 now explicitly requires the relevant cold-build measurements and SDK packaging decisions. |
| 21 | Resolved | Exact archive inspection replaces the unsupported blanket assertion. |
| 22 | Resolved | Building all linked objects against the baseline, disabling dependencies, and auditing imports addresses the design issue. |
| 23 | Resolved | Explicit VFS setup and relocated tests against real headers are appropriate. |
| 24 | Partly | Real GCC tests on every platform are required. Minimum-OS execution tests, Windows argument/path cases, and an actionable Intel-runner replacement remain outstanding. |
| 25 | Resolved | Compiler identification and effective-dialect probing replace basename/default assumptions. Exact release-version claims still require D1 verification. |
| 26 | Partly | The equivalence harness moves earlier. Representative-library evaluation still occurs after choosing the pruning rules, and containment arrives late. |
| 27 | Partly | Notices and revisions are included. A concrete corresponding-source distribution mechanism and clean minimum-platform tests are still missing. |

The following are the remaining findings, including problems introduced by the rewrite.

1. **Blocker — “known undefined” cannot be established from local system headers.**

   Consider two otherwise supported environments:

   ```cpp
   // Local <platform_api.h>: does not define PLATFORM_API
   // Judge <platform_api.h>: #define PLATFORM_API 1

   #include <platform_api.h>
   #if PLATFORM_API
   int main() { return 0; }
   #else
   int main() { return 1; }
   #endif
   ```

   If no local system header defines `PLATFORM_API`, v2 classifies it as undefined and retains the second branch. Original and transformed programs agree locally, including runtime checks. On the judge, the original returns 0 and the transformed program returns 1.

   A non-reserved spelling does not prevent a system header from defining it. “The judge passes only `defines`” establishes, at most, facts about command-line definitions—not definitions introduced by headers or the compiler.

   Related profile problems:

   - A matching target triple does not establish matching compiler flags, headers, runtime, or predefined macros.
   - GCC family/version alone does not establish the MinGW environment.
   - `__cplusplus` must not be frozen from the local compiler’s default. Even a dialect label needs care for compilers implementing draft standards with different macro values.
   - Precedence between the profile’s `std`, the command’s `-std`, and local defaults is unspecified.

   **Fix:** Separate the judge configuration from the local analysis/check configuration. Give each resolved fact explicit target provenance. Treat header effects as unknown unless covered by a target-specific contract. A local header scan can discover possible definitions; it cannot prove their absence on the judge.

2. **Blocker — tri-state macro values are not a sufficient preprocessing model.**

   Preserving directives does not make it safe to ignore their effects:

   ```cpp
   #define MODE 1
   #pragma push_macro("MODE")
   #undef MODE
   #define MODE 2
   #pragma pop_macro("MODE")

   #if MODE == 1
   int main() { return 0; }
   #else
   int main() { return 1; }
   #endif
   ```

   The specified resolver sees the last `#define` as 2 unless it models the stack operation. The compiler sees 1. GCC explicitly supports this restoration behavior. [GCC macro-stack pragmas](https://gcc.gnu.org/onlinedocs/gcc/Push_002fPop-Macro-Pragmas.html)

   Two further traps matter:

   ```cpp
   #if 1 || __COUNTER__
   #endif
   int main() { return __COUNTER__; }
   ```

   Macro expansion consumes the first counter even though arithmetic evaluation short-circuits. Removing the condition changes the return value from 1 to 0. Disabling **pruning** does not protect against **resolution** doing this.

   Also, an unknown replacement list is not an unknown scalar:

   ```cpp
   #if 0 && X
   ```

   If `X` expands to `0 || 1`, the expression is true. A Boolean tri-state evaluator must not conclude false by treating `X` as an atomic unknown operand. Macro expansion precedes expression evaluation. [GCC conditional-expression rules](https://gcc.gnu.org/onlinedocs/cpp/If.html)

   Includes inside unresolved branches must contribute their possible macro effects to the merge, even when there are no textual `#define`s in that branch. The same applies to `_Pragma` and possible undefinitions.

   **Fix:** Specify preprocessing-token expansion, full macro definitions including parameters, macro stacks, expansion side effects, and conservative branch merges. Separate definedness from replacement-list knowledge. Unsupported effects must inhibit resolution of affected regions; stateful expansion may require preserving the original directive itself.

3. **Major — the resolver also discards too much useful knowledge.**

   The initial-state rule excludes macros defined anywhere in user code from “known undefined.” Consequently, ordinary include guards and synthesized `DISTILL_ONCE_*` guards start unknown—even before their first definition.

   Then, every system include invalidates every reserved macro. A typical initial `<bits/stdc++.h>` can therefore erase the profile’s compiler/platform facts before most library conditionals are reached.

   Finally:

   ```cpp
   #if UNKNOWN
   #define MODE 1
   #else
   #define MODE 1
   #endif
   ```

   leaves `MODE` unknown despite identical branch results.

   These choices are conservative in isolation, but undermine the user’s stated reason for profiles.

   **Fix:** Track macro state at each program point rather than using “defined somewhere in the file.” Join identical branch states, distinguish definitely-defined/unknown-value from unknown-definedness, and introduce explicit target header-effect contracts where justified. Measure how many profile conditions remain resolvable after normal system includes.

4. **Blocker — textual substitution does not preserve all file-dependent preprocessing.**

   This supported input defeats the claimed universal `raw.cpp` guarantee:

   ```cpp
   #if __has_include("optional.hpp")
   #include "optional.hpp"  // defines answer() returning 1
   #else
   int answer() { return 0; }
   #endif

   int main() { return answer(); }
   ```

   In the original source tree, the header exists. In a submission containing only the generated `.cpp`, it does not. V2 preserves the `__has_include` condition, so the judge selects the fallback even though the header’s contents were embedded.

   Local checks may all pass because the original header remains available through `-I`.

   Other file-boundary issues include:

   - Quote and angle includes require different search chains; the prose currently describes the quote chain as though it applied to both.
   - A retained quoted system include can acquire a different first search directory after flattening.
   - `#line` does not restore include-search directory semantics.
   - File-scoped pragmas need individual treatment. For example, `#pragma GCC system_header` has different behavior in a header and the primary source file. [GCC include search](https://gcc.gnu.org/onlinedocs/cpp/Search-Path.html), [system-header semantics](https://gcc.gnu.org/onlinedocs/cpp/System-Headers.html)

   **Fix:** Include header-availability queries and file-scoped directives in the supported-input contract. Rewrite demonstrably captured user-header queries where sound, otherwise reject the unsupported transformation. Verify self-containment in an environment where the original user headers are unavailable.

5. **Major — the duplicate-inclusion predicate is insufficient, and guarded recursion is not an algorithm.**

   Suppose `h.hpp` is wholly guarded by `H`:

   ```cpp
   #define H
   #include "h.hpp"  // unconditional, but its contents are skipped
   #undef H
   #include "h.hpp"  // must now contribute its contents
   ```

   The first occurrence satisfies the stated “outside every conditional block” condition. That does not justify deleting the second.

   The parenthetical promise to handle undefined guards conflicts with the actual predicate. The relevant fact is the guard’s state **at the later occurrence**, including effects from intervening headers and pragmas. GCC’s own guard optimization depends on the controlling macro still being defined. [GCC once-only headers](https://gcc.gnu.org/onlinedocs/cpp/Once-Only-Headers.html)

   Guarded include cycles also need an explicit stopping rule. “Reject cycles without a guard” does not explain how expansion terminates with a guard whose state is uncertain.

   Conditional or late `#pragma once` cannot automatically become an unconditional whole-file guard. Canonical paths also do not capture every filesystem alias, notably hard links.

   **Fix:** Initially remove ordinary duplicate-elimination optimization. Preserve guarded occurrences and terminate cycles only with a proven dominating guard state or a narrowly specified supported pattern. Restrict pragma-once conversion to proven supported forms, with filesystem identity and synthesized-name collision handling.

6. **Blocker — the location policy is incomplete even for ordinary `__LINE__`.**

   ```cpp
   int unused;
   int main() { return __LINE__; }
   ```

   Removing the first line changes behavior. Markers at **splice boundaries** do not repair deletions within a source segment, and this example has no splice.

   Resolution and comment stripping can also change line positions. `std::source_location` can observe columns, which `#line` alone cannot restore.

   The detection rule misses location use hidden in system macros and compiler builtins. Moreover, “file names as the user wrote them” is not GCC’s `__FILE__` behavior: GCC reports the path used to open the file, which may include the search-directory prefix. [GCC predefined location macros](https://gcc.gnu.org/onlinedocs/cpp/Standard-Predefined-Macros.html)

   **Fix:** Maintain locations through the final rendering stage, including resolution and deletion. Preserve columns where observable or reject unsupported cases. Account for existing `#line`, builtins, system-macro expansion, and timestamp macros. Define the intended logical filename semantics across machines.

   `--strip-comments` must preserve token separation and necessary newlines, and the **actual final bytes** must be checked. Deleting `/**/` without replacement can change tokens or whether a macro is function-like.

7. **Blocker — determining roots from the local AST can itself violate the target-independence requirement.**

   This example needs no unresolved preprocessor branches:

   ```cpp
   int count = 0;

   constexpr int initialize(bool small_long) {
       if (small_long)
           ++count;
       return 0;
   }

   int unused = initialize(sizeof(long) == 4);

   int main() { return count; }
   ```

   On a usual LP64 development machine, `unused` has constant initialization and a trivial destructor. If the proposed AST supplies root classification, neither `unused` nor `initialize` needs to survive.

   On a Windows LLP64 judge, the initializer increments `count`. Original and candidate both compile, but return different values. Local runtime comparisons also pass.

   This is not remedied by name matching: `unused` is never named elsewhere. A profile does not fix it if parsing still uses the local check compiler’s target.

   **Fix:** Require initialization/destruction root decisions to hold across the supported target configurations. Without that proof, retain potentially effectful globals and their dependencies. An exact target parse can improve precision, but successful local constant evaluation cannot justify removal.

8. **Blocker — the association rule misses generic implicit operations.**

   Without a profile:

   ```cpp
   struct S {};

   template<class T>
   int operator+(T, T) { return 7; }

   #ifdef __clang__
   int main() { return 0; }
   #else
   template<class U>
   constexpr bool addable = requires(U u) { u + u; };

   int main() { return addable<S> ? 0 : 1; }
   #endif
   ```

   In the Clang parse:

   - The GCC branch remains text, but creates no AST references.
   - `S` is retained by its identifier.
   - The generic operator’s signature names no kept record.
   - Neither `operator+` nor its template parameter `T` appears as an identifier in retained text.

   The operator can therefore disappear. GCC still compiles the candidate: `addable<S>` simply becomes false. The return value changes from 0 to 1. Clang runtime checks pass; GCC compile-only checks pass; the identifier invariant passes.

   Similar gaps exist for generic `begin`/`end`, customization functions, literal operators, and other implicit lookup mechanisms. Aliases and template wrappers also require semantic association, not merely spelling.

   **Fix:** Conservatively retain generic candidates for implicit lookup where unresolved code can use them. Initially, pin relevant operator/customization families or disable pruning when the needed candidate set cannot be bounded. The association rule is a useful retention heuristic, not a soundness argument.

9. **Major — dependencies through retained system code and family ownership remain undefined.**

   “Visit references in kept units” does not specify what happens when a reference reaches a system declaration with no editable unit. Its body, instantiations, defaults, or callbacks may reference user declarations.

   Likewise, a user specialization can belong to a family whose primary template lives in a system header. There must be an entity graph beyond the collection of editable units.

   Records, template families, overload sets, and declaration groups also overlap. A record and its out-of-line members usually occupy multiple disjoint ranges; treating their ownership as one contiguous deletion span would erase unrelated text.

   **Fix:** Separate semantic entities, indivisible retention families, and editable ranges. Traverse dependencies through noneditable nodes. Merge overlapping retention obligations without merging unrelated source spans. Conservatively retain unsupported assembly/linker references and effectful attributes.

10. **Major — the name/association closure may retain a large fraction of a typical library.**

   I cannot establish how much without the corpus. There are nevertheless clear amplification paths:

   - Unqualified identifier matching crosses namespaces and confuses local names with unrelated global names.
   - Keeping all macro bodies roots every declaration named by an unused macro.
   - A retained record keeps every member body and their dependencies.
   - Associating every function mentioning `Mint` can retain substantial polynomial/NTT code merely because `Mint` is used.
   - Entirely unparsed duplicate header bodies contain declaration names that can retain their parsed counterparts.
   - If “names a unit declares” includes member names such as `size`, `value`, or `begin`, retention spreads further.

   There is also a fundamental distinction between an identifier appearing and an entity being needed. The final name invariant is a useful edit check, but cannot certify equivalence.

   **Fix:** Precisely define indexed names, including nested members, template parameters, operators, and system records. Bring a representative library corpus into D2/D3 and report bytes retained by each reason. Preserve the conservative mode; improve precision only with demonstrated semantics. Do not promise useful size reduction before measuring this closure.

11. **Blocker — the check ladder compares different configurations and calls expected differences bugs.**

   ```cpp
   #ifdef ONLINE_JUDGE
   int main() { return 1; }
   #else
   int main() { return 0; }
   #endif
   ```

   With a profile defining `ONLINE_JUDGE`, resolution correctly leaves the first program. But an ordinary local baseline command without that definition returns 0.

   Thus `--verify-run` reports a transformation bug when resolution was correct. Merely selecting a cross compiler does not supply the judge’s command-line definitions.

   Platform-specific branches create the same problem. Judge-specialized `flat.cpp` may legitimately stop building locally even though the transformation is correct for the judge.

   **Fix:** Define separate validation configurations:

   - Compare original and raw under the same available configuration.
   - Compare original, flat, and candidate under a configuration matching the assumptions used for resolution.
   - If that configuration cannot run locally, report unavailable target runtime verification; do not substitute a local execution as equivalent evidence.

   A mismatching local check may remain useful diagnostically, but cannot be the acceptance criterion for target-specialized output.

   Also reconcile the outcome rules: §2 says unchecked writes flat, §4.6 says raw; one paragraph says any flat failure means `failed`, another permits raw fallback. Track artifact choice and verification status separately. Baseline compilation failure is not automatically evidence that the input is invalid, nor permission to publish an artifact whose structural transformation is unsupported.

12. **Major — the command model must control outputs and reject unsupported semantics.**

   Passing everything else through only to verification is too broad. Options such as sysroots, packing, feature flags, forced preprocessing inputs, and optimization-dependent settings can change parsing or initialization decisions.

   “Replace the source file in the whole command” also leaves unresolved:

   - `-c`, `-S`, and `-E`, which do not produce a runnable executable;
   - output and dependency paths that would be overwritten repeatedly;
   - response files, wrappers, and multiple inputs;
   - preservation of source-relative include lookup when moving temporary files;
   - native Windows argument quoting—`CreateProcess` ultimately receives a command-line string.

   **Fix:** Define accepted driver invocations and normalize probes, builds, links, and output paths separately. Preserve semantic argument order, isolate generated artifacts, and implement Windows quoting explicitly. Unsupported semantic options must disable pruning or be rejected, rather than merely appearing in a report.

13. **Major — workers need to contain the whole expensive pipeline and its descendants.**

   Killing a worker does not necessarily kill its compiler, linker, or sample-program descendants. Inherited pipes can also remain open after the direct child exits.

   The text inliner and resolver run before the specified worker stages. Repeated expansion can consume enormous memory; malformed recursion handling can hang. A total deadline does not interrupt arbitrary in-process work by itself.

   **Fix:** Specify process-group/Job Object containment, descendant termination, concurrent pipe draining, aggregate output limits, and cleanup/reaping. Contain or cooperatively bound probing, baseline builds, inlining, and resolution too. Add limits for include expansion, generated text, and macro expansion.

   A worker failure must identify which artifact and checks completed. Never promote a fallback merely because the deadline expired before checking it. Move this machinery into the first usable pipeline milestone.

14. **Major before release — remaining distribution obligations need concrete acceptance criteria.**

   The release plan should explicitly require execution on supported minimum platforms, tests without incidental build-machine dependencies, and a published source-distribution mechanism.

   License notices and build instructions are useful but do not by themselves specify how corresponding source will be provided with binary downloads. Define that mechanism in release preparation. [GPLv3, section 6](https://www.gnu.org/licenses/gpl-3.0.html#section6)

   **Fix:** Add these as release gates, alongside the already planned SDK measurements and dependency audits. This need not block an isolated D1 feasibility spike.

**Verdict:** v2 is ready for a distribution spike, but not for implementation of the semantic core as settled design. Before D2, fix the target-fact model, preprocessing effects, file-boundary contract, location handling, validation configurations, and command/worker behavior. Before D3, establish conservative rules for target-dependent roots and implicit lookup, then measure retention on real libraries.

What would change my assessment is a revised supported-input contract plus passing adversarial tests for the examples above—especially tests where every candidate still compiles. Exact judge toolchains would improve confidence, but would not independently fix the text-inlining or generic-lookup holes.