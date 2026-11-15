**I would not approve v3 as the settled specification for D2 or D3 yet.** It improves the architecture substantially, but several new rules still permit silent behavior changes. The most serious are the header-macro transitions and the definition of an inert variable.

I assume equivalence means comparing the original source tree and generated file under the **same judge configuration**, for programs with defined behavior. I also assume the user accepts §1.1’s exclusions; those narrow the original requirement and cannot simply be inferred from it. The examples below are reasoned counterexamples, not tests I executed against an implementation.

“Resolved” means resolved at the design level.

| R2 item | Status | Assessment |
|---|---|---|
| 1. Target facts and header effects | **Partly** | Configuration separation and profile dialect precedence improve matters. The new standard-header transition is unsound, and matching a triple still proves too little. |
| 2. Preprocessing state and effects | **Resolved** | Replacement-list knowledge, branch joins, and disabling transformations for the named stateful constructs address the specific examples. Expansion and unsupported-feature boundaries still need an implementable specification. |
| 3. Excessive loss of knowledge | **Partly** | Equal-state joins and preserving profile facts help. Without a profile, ordinary guards still start unknown; the unconditional claim that first inclusions resolve is incorrect. |
| 4. File-dependent preprocessing | **Partly** | Availability-query rejection, separate lookup chains, and removing user include paths from checks help. Computed system includes and unavailable alternative headers remain insufficiently specified. |
| 5. Repeated inclusion and recursion | **Partly** | Preserving repeated inclusions is correct. The recursive-inclusion predicate still does not establish the guard’s current state when system headers or macro-stack operations can alter it. |
| 6. Locations and rendering | **Partly** | Final-render location maintenance and comment separation are improvements. Builtin and system-macro location observations remain uncovered. |
| 7. Target-independent variable roots | **Partly** | The original local constant-evaluation mistake is avoided. The replacement inertness predicate is independently unsound. |
| 8. Generic implicit operations | **Partly** | The original generic-operator example is covered. Operations involving only system types, and other dependencies hidden from identifier matching, remain exposed. |
| 9. Entity graph and family ownership | **Partly** | Entities, families, disjoint ranges, and traversal through system instantiations are the right architecture. Overlapping retention obligations and the full dependency traversal still need explicit rules. |
| 10. Retention explosion | **Partly** | Early corpus measurement is appropriate. Indexed names and the meaning of retaining “conditional blocks” remain consequential ambiguities. |
| 11. Validation and outcomes | **Partly** | The main same-configuration comparison is fixed in principle. Mismatching local fallback checks, target qualification, nondeterminism, and exit/write precedence remain inconsistent. |
| 12. Command model | **Partly** | Structured invocation and controlled executable/dependency outputs improve matters. Broad “harmless” prefixes admit preprocessing and linker controls. |
| 13. Limits and containment | **Resolved** | Pipeline-wide limits, process groups/Job Objects, pipe draining, and checked-only fallback are sufficient design commitments. |
| 14. Distribution acceptance | **Partly** | Source distribution and dependency audits are more concrete. Running on the oldest available macOS runner does not validate the advertised macOS 12 minimum. |

The following findings are the main remaining blockers, including defects introduced by v3’s fixes.

1. **Blocker — the standard-header rule invents both definedness and absence.**

   These are two different errors.

   First, including *any* listed standard header cannot make *all* standard macro names definitely defined:

   ```cpp
   #include <cstddef>
   #ifdef assert
   int main() { return 1; }
   #else
   int main() { return 0; }
   #endif
   ```

   On a configuration where `<cstddef>` does not expose `assert`, the original returns 0. V3 sets `assert` to “defined with unknown replacement,” resolves `#ifdef assert` true, and produces a program returning 1. Both compile.

   Even a header-specific list must distinguish **guaranteed**, **optional**, and **possibly transitively introduced** macros.

   Second, real GNU library environments expose non-ISO macros through standard headers:

   ```cpp
   #include <math.h>
   #ifdef M_PI
   int main() { return 1; }
   #else
   int main() { return 0; }
   #endif
   ```

   Under GNU configurations exposing `M_PI`, the original returns 1. V3 can preserve its initial “undefined” state because it is neither an ISO macro nor a macro attributed to an extension header. GNU libc explicitly documents these mathematical macros and their feature-selection conditions. [GNU libc mathematical constants](https://www.gnu.org/software/libc/manual/2.30/html_node/Mathematical-Constants.html)

   Scanning libstdc++ once per version does not establish the transitive effects of different libc versions, operating systems, feature modes, or vendor patches. Reporting an assumption does not make it compatible with the correctness requirement.

   The initial state has a related problem: GNU modes can predefine non-reserved names such as `unix`. “The judge passes only these `-D` options” does not establish absence of compiler-predefined macros. [GCC system-specific predefined macros](https://gcc.gnu.org/onlinedocs/cpp/System-specific-Predefined-Macros.html)

   **Fix:** Separate “may change this macro” from “guarantees this macro is defined.” Use versioned, configuration-specific header contracts where available; otherwise invalidate conservatively. Establish initial absence from a supported toolchain contract, not spelling alone. Profiles can still preserve explicitly guaranteed platform facts.

2. **Blocker — unknown headers must also invalidate user-defined macros.**

   V3 exempts macros “defined by the user” from invalidation after an arbitrary system include. That exemption is unjustified:

   ```cpp
   #define MODE 1
   #include <vendor_config.h>  // #undef MODE, then #define MODE 2

   #if MODE == 1
   int main() { return 1; }
   #else
   int main() { return 2; }
   #endif
   ```

   The specified transition retains `MODE == 1`; the actual preprocessor sees 2.

   This also undermines recursion handling. A system header can undefine an enclosing user header’s guard. A textual preceding `#define`, plus absence of a user-text `#undef`, does not prove that a recursive inclusion is skipped. Macro-stack restoration can produce the same issue even when transformations have been disabled and the output is nominally `raw`.

   **Fix:** An unknown header effect must invalidate any macro it could modify, regardless of provenance. Recursive inclusion needs a proven guard state across intervening effects, or rejection. A narrow rule allowing only intervening directives with modeled effects is implementable.

3. **Blocker — “no calls” does not mean inert, and constant initialization does not mean harmless destruction.**

   This meets the proposed initializer test:

   ```cpp
   int count = 0;
   int unused = ++count;
   int main() { return count; }
   ```

   `unused` has a scalar type and an initializer containing no function call, constructor call, or lambda. Removing it changes the exit status from 1 to 0.

   Assignment and compound assignment have the same problem. Implicit initialization matters too:

   ```cpp
   int count = 0;
   struct S {
       S() { ++count; }
   };
   S unused;
   int main() { return count; }
   ```

   “No initializer” must not exempt implicit constructor execution.

   The `constinit` alternative is also unsafe if, as written, it independently establishes inertness:

   ```cpp
   #include <cstdio>
   struct X {
       constexpr X() {}
       ~X() { std::puts("bye"); }
   };

   constinit X unused;
   int main() {}
   ```

   Removing `unused` removes output. `constinit` establishes static initialization, not trivial destruction. [C++ draft: `constinit`](https://eel.is/c++draft/dcl.constinit)

   If the destructibility requirement was intended to apply to every alternative, say so explicitly. Also, “records whose members are all scalars” is not a sufficient test: the record may have a user-provided destructor or a nontrivial base. `constexpr` likewise must not bypass the destruction check.

   **Fix:** Require two separate proofs: harmless initialization and harmless destruction, including implicit operations and lifetime-extended temporaries. Start with a small positive whitelist—such as suitable scalar constant initializations—and retain everything else. Do not replace local constant evaluation with an equally unsafe syntactic blacklist.

4. **Blocker — the generic-customization rule still misses supported implicit operations.**

   A generic operator can matter when **no user record is retained**:

   ```cpp
   #include <utility>

   template<class T>
   int operator+(T, T) { return 7; }

   #ifdef __clang__
   int main() { return 0; }
   #else
   template<class U>
   constexpr bool addable = requires(U u) { u + u; };

   int main() {
       return addable<std::pair<int, int>> ? 0 : 1;
   }
   #endif
   ```

   Without a profile:

   - Clang parses the first `main`.
   - The GCC branch stays as text.
   - No retained user record triggers rule 4.
   - The operator’s name does not appear as an identifier in the retained expression.
   - No active AST reference keeps it.

   Removing the operator changes the GCC result from 0 to 1 while both versions compile. A concrete non-member operator on a standard-library type creates a similar gap.

   There is another dependency hole that the generic rule cannot address:

   ```cpp
   #define CAT(a, b) a##b
   int chosen1(int) { return 0; }

   #ifdef __clang__
   int main() { return 0; }
   #else
   template<class T>
   constexpr bool available = requires(T x) { CAT(chosen, 1)(x); };

   int main() { return available<int> ? 0 : 1; }
   #endif
   ```

   The inactive text contains no `chosen1` identifier. Keeping macro definitions and macro-generated *declarations* does not protect a declaration referenced through a pasted *use*. Removal again changes a `requires` result without causing a build failure.

   **Fix:** Initially retain all user non-member operator families when configurations remain unanalyzed, including operators on system types. Treat token pasting and unknown expansion in potentially active code as a reason for broader retention or disabling pruning. A finite customization-name list is a heuristic unless its completeness is justified for the supported constructs.

5. **Blocker — accepted computed system includes can defeat self-containment.**

   V3 rejects computed includes only when they resolve to a user header. Resolution is configuration-dependent:

   ```cpp
   #ifdef _WIN32
   #define HEADER "win_impl.hpp"
   #else
   #define HEADER <vector>
   #endif

   #include HEADER
   ```

   On Linux, the include resolves to a system header and appears acceptable. On Windows, the generated submission still needs `win_impl.hpp`. Disabling condition resolution does not fix the missing inline.

   **Fix:** Reject computed includes unless every possible resolution within the supported configurations is proved to be an allowed system include. For v1, rejecting all computed includes is a reasonable simpler boundary.

   Likewise, define what happens when a literal include in an unresolved branch is unavailable locally. “Follow the compiler’s rules” does not specify whether it is an unavailable user dependency, an unavailable system header, or an unsupported input.

6. **Major — location preservation still misses directly supported observations.**

   ```cpp
   int unused;
   int main() { return __builtin_LINE(); }
   ```

   No `__LINE__` token activates markers. Deletion can change the return value. Both GCC and Clang expose source-location builtins; Clang also exposes a column builtin. [Clang source-location builtins](https://clang.llvm.org/docs/LanguageExtensions.html#source-location-builtins)

   A retained system macro expanding to `__LINE__` has the same problem. Rejecting `std::source_location` does not cover these observations.

   **Fix:** Preserve logical line mappings unconditionally, or establish a complete supported-observation policy that includes builtins and system-macro expansion. Reject unsupported column observations. Existing `#line` directives must participate in the mapping.

7. **Blocker — validation still accepts a configuration mismatch as stronger evidence than it is.**

   Consider a Windows profile and a Linux local compiler:

   ```cpp
   #ifdef _WIN32
   int main() { return 1; }
   #else
   int main() { return 0; }
   #endif
   ```

   The correct specialized file returns 1 when compiled locally. The original compiled locally returns 0, even with the profile’s judge defines. This difference is expected.

   Thus the fallback paragraph needs an explicit rule: a mismatching local build is a **diagnostic build check**, and its runtime comparison cannot establish transformation equivalence or a distill bug.

   Triple matching is also insufficient. A profile may resolve `__GNUC__ >= 14`, while an available GCC 12 cross compiler targets the same triple. Calling that compiler the “resolution configuration” produces either false bug reports or overstated `target` verification.

   Local `-D`/`-U` options need explicit removal or replacement in target builds. Merely adding profile defines leaves cases such as local `-DLOCAL` or `-UONLINE_JUDGE` ambiguous.

   **Fix:** Define an explicit configuration object and require a validating compiler/configuration to satisfy every fact used by the transformation. Record build and runtime-check status separately. Treat mismatching local checks as diagnostics, not equivalent substitutes.

   The result model also needs a small complete decision table:

   - Exit 2 appears unreachable under the rule that unchecked artifacts cannot be selected.
   - §2 describes exit 3 after writing a nonconforming strict result; §4.6 suggests strict mode forbids writing the fallback.
   - A nondeterministic runtime difference is described both as a fallback condition and as a distill bug.

   I would remove unchecked writes, define one strict-mode write policy, and classify unexplained runtime differences as failed or inconclusive verification—not automatically a proven implementation bug. Reading randomness does not necessarily make stdout nondeterministic, and the tool cannot reliably detect all such behavior.

8. **Major — the “harmless” argument prefixes are not harmless categories.**

   For example:

   ```text
   -Wp,-include,config.hpp
   -Wl,-Map,out.map
   ```

   Both match `-W*`. The first injects preprocessing input; the second writes an additional output. Neither fits the promised treatment of ordinary warning options. Optimization and instrumentation flags can also alter predefined macros or program behavior.

   **Fix:** Parse exact option families and their arguments. Handle or reject preprocessor/linker forwarding options; separately control auxiliary outputs. “Harmless” should mean harmless to the specific analysis model, not recognizable by a broad spelling prefix.

   This is a command-model correction before D2, not merely a test case to add afterward.

V3 is consequently **not yet implementable without substantial semantic choices being made during coding**. Two boundaries must be specified first:

- The resolver needs a supported abstract-expansion model: function-like macros, rescanning, pasting/stringification, variadics, unknown expansions, conditional joins, and target-dependent preprocessing arithmetic. Full support is optional; a sound “unknown, retain the original block” outcome is not.
- Pruning needs explicit rules for combining overlapping families, which declaration names are indexed, and what is protected in unresolved conditionals. Overlapping obligations can be combined transitively while preserving separate source ranges; that is a routine implementation technique once the intended semantics are stated.

The ordinary AST traversal details can be developed with tests. The meaning of “proof,” “unknown,” and “retain this conditional block” cannot safely remain implementation choices.

On usefulness, **I would not call pruning pointless across typical CP libraries, but the current rules can make it nearly pointless for particular library structures.**

| Library structure | Likely result |
|---|---|
| Independent graph algorithms and utility functions | Meaningful removal is plausible when they have distinctive names and no rooted globals. |
| Segment-tree and other class templates | Unused classes can disappear; a used class retains all members and their dependencies. |
| Modint plus polynomial/NTT helpers | High retention: association with the retained modint type can preserve many unrelated operations. |
| FFT/NTT with dynamically initialized tables | Globals and their initialization dependencies may retain substantial modules. |
| Fast I/O with global initializer/destructor objects | Much of the module may correctly remain rooted. |
| Macro-heavy libraries and common names such as `size`, `value`, `begin` | Potentially extensive accidental retention, depending on the indexed-name definition. |

The largest immediate size problem is **ordinary include guards without a profile**. Their initial state is unknown under §4.5, contrary to the later unconditional first-inclusion claim. If “conditional blocks are never removed” means their entire contents are protected, guarded headers largely defeat pruning. If it means only the directives are protected, say so and define the treatment of each branch.

Generic-operator retention alone is unlikely to make pruning useless. Whole-record retention, global initialization roots, broad association, and identifier collisions are the more important amplifiers. I would expect useful reductions for modular libraries and much weaker reductions for dense umbrella libraries, but would not attach percentages without the corpus.

My prioritized conditions for proceeding are:

1. **Before D2:** Replace the macro absence/header-effect rules with sound contracts and conservative fallback; specify the supported macro-expansion subset.
2. **Before D2:** Close the raw-inlining boundary—computed includes, recursive guards across unknown effects, and location observations—and correct command-option classification.
3. **Before D2:** Finalize configuration matching and the artifact/write/exit decision table. A mismatching local check must never certify judge equivalence.
4. **Before D3:** Replace inertness with a sound positive proof, cover implicit lookup and pasted references conservatively, and define family/name/conditional retention semantics.

During implementation, settle exact AST visitors, range bookkeeping, profile-contract coverage, resource limits, and retention precision through adversarial tests and corpus measurements. Keep the macOS minimum-version evidence and exact LLVM/distribution claims as release gates; they need not block D2.

**D1 remains independently reasonable. D2 becomes ready after items 1–3; D3 after item 4.** Passing regression tests where every original and candidate still compiles would materially improve my assessment. Exact judge toolchains would strengthen validation, but would not repair the inertness, macro-state, or hidden-reference counterexamples.