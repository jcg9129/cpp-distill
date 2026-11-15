**V4 is not ready to be the settled specification for D2 or D3.** It fixes several R3 counterexamples, but still permits silent behavior changes. The strongest remaining defects are the treatment of profile facts across headers, the claimed strength of `-E -P` equality, recursive inlining across macro-stack operations, and configuration-dependent inertness.

I accept A1–A4 and §1.1 for this review. I assume the original has defined behavior in the configuration being compared, and that correctness must hold without optional sample runs. The counterexamples below are reasoned examples; I have not executed them against an implementation.

**1. Disposition of R3**

| R3 item | Status | Assessment |
|---|---|---|
| 1. Standard-header effects and initial absence | **Partly** | “May define” becoming unknown fixes the original `assert`/`M_PI` errors. Exempting profile facts from header invalidation is unsupported, and the initial absence rule needs an exhaustive predefined-name contract. |
| 2. Unknown headers and recursive guards | **Partly** | Invalidating every macro after an arbitrary system include is correct. Recursive inclusion can still be dropped across `pop_macro`, including in raw-only mode. |
| 3. Inert variables | **Partly** | The explicit increment, constructor and destructor examples are fixed. The whitelist still needs a configuration-independent proof of the variable’s type and initializer. |
| 4. Implicit operations and hidden references | **Partly** | Retaining every non-member operator fixes the specific removal example. Lookup-enabling declarations and references introduced by system macros remain uncovered. |
| 5. Computed and unavailable includes | **Resolved** | Rejecting all computed user includes and unavailable quoted includes supplies the requested explicit boundary. |
| 6. Location observations | **Partly** | Direct builtin observations are covered. Observations introduced by system declarations or unobserved macro expansions remain outside the activation rule. |
| 7. Validation and outcomes | **Partly** | Configuration separation, local define replacement, strict-mode behavior and runtime status are substantially resolved. Fact satisfaction is incorrectly defined for derived states, and token equality is overstated. |
| 8. Command-option classification | **Resolved** | The specific broad-prefix problem is fixed at the design level. Exact parsing and propagation of preprocessing-affecting options need regression tests. |
| Must specify: abstract expansion | **Partly** | Delegating expansion to Clang is reasonable, but the synthetic environment, definition-only tests, parameter handling and conditional joins still need explicit semantics. |
| Must specify: family/name/conditional retention | **Resolved structurally** | Overlapping obligations, indexed names and directive retention are now specified. The resulting rules still have semantic dependency holes, described below. |

**2. D2 problems**

**A. Blocker: A1–A4 do not make profile facts immutable across standard headers.**

The may-define set excludes reserved names that are profile facts. But:

- A1 permits headers to define reserved names.
- A2 does not prohibit changing reserved macros.
- A4 establishes the profile’s accuracy; it does not say a compiler-predefined macro survives every subsequent include.

For a concrete adversarial fixture, take an otherwise suitable standard header that ends with:

```cpp
#undef __GNUC__
#define __GNUC__ 99
```

This is a test of the stated assumptions, not a claim about stock libstdc++. The modification is permitted by A1–A2. A profile accurately describing GCC 14 still describes its initial compiler identity accurately.

Then:

```cpp
#include <cstddef>

#if __GNUC__ == 14
int main() { return 1; }
#else
int main() { return 2; }
#endif
```

V4 preserves the initial fact and resolves to 1. The actual preprocessor selects 2. Both compile.

Ordinary token equality catches this particular manifestation when that header is available to the validating compiler. It does not repair the transformation’s justification for another allowed header implementation—and finding B shows how the check can miss the error even locally.

**Fix:** Header transitions must invalidate every name the header is allowed to change, including names whose initial values came from a profile. Preserve a fact only under a separately established preservation contract. Its provenance as a compiler fact is insufficient.

Two related contract details need tightening:

- A1 permits “names the standard specifies,” while the may-define set says “the standard’s macro names.” Those must denote the same explicitly defined permitted set, or the transition must conservatively cover the larger set.
- “Reserved” must have one consistent definition. C++ reserves identifiers containing `__` anywhere, not merely those starting with it. [C++ draft, reserved identifiers](https://eel.is/c++draft/lex.name)

**B. Blocker: `-E -P` token equality can certify different exit statuses.**

Modify the previous example:

```cpp
#include <cstddef>
#if __GNUC__ == 14
int main() { return __builtin_LINE(); }
#else
int main() { return __builtin_LINE(); }
#endif
```

With the header fixture above:

- The original selects the definition on line 5.
- The resolver selects the definition on line 3.
- Correctly maintained `#line` mappings preserve those different logical lines.
- Both `-E -P` results contain the same tokens:

```cpp
int main() { return __builtin_LINE(); }
```

The compiler subsequently evaluates the builtin to different values. Both executables build, the initial probe agrees with GCC 14, and the preprocess comparison passes. Without samples, the wrong artifact can be written.

GCC documents that `__builtin_LINE()` is evaluated as a source-location builtin, including call-site evaluation in default arguments. `-P` suppresses the line markers that would expose the distinction. [GCC location builtins](https://gcc.gnu.org/onlinedocs/gcc-8.1.0/gcc/Other-Builtins.html), [GCC preprocessing options](https://gcc.gnu.org/onlinedocs/gcc/Preprocessor-Options.html)

Thus the statement “if the preprocessor takes a different branch, the token streams differ” is false.

**Fix:**

- Describe token equality as one check, not an empirical proof of A1–A3 or of resolver correctness.
- Preserve and check relevant logical source locations, including locations consumed by compiler builtins.
- Check the macro-state obligations used by resolution directly where possible: initial absence against the probe, and header effects against preprocessing events.
- Retain directive boundaries and relevant pragma information in the comparison representation; an undifferentiated token sequence is not a complete compiler input model.

This does not require detecting hypothetical violations that have no observable consequence. It does require detecting the concrete, observable violation above.

**C. Blocker: an empty-file probe cannot satisfy source-derived macro states.**

Consider an ordinary guarded header included twice:

```cpp
#ifndef MY_HEADER
#define MY_HEADER
// declarations
#endif
```

Resolution uses `MY_HEADER` as undefined at the first inclusion and defined at the second. An empty-file probe cannot agree with both states.

Even this is problematic:

```cpp
#define MODE 7
#if MODE == 7
int main() { return 0; }
#else
int main() { return 1; }
#endif
```

The validating compiler need not predefine `MODE=7`. The source establishes it. Under a literal reading of §4.6, the useful specialization is unnecessarily rejected.

Conversely, collecting only the final macro value loses the actual dependencies that justified the transformation.

**Fix:** Separate:

1. **External requirements:** initial predefined values or absence, dialect and arithmetic properties.
2. **Derived states:** source definitions, undefinitions and branch joins.
3. **Assumption obligations:** for example, a header preserving a particular macro.

A configuration satisfies the external requirements. The resolver derives source states from those requirements and the source; they are not independently compared with an empty-file probe.

Also record non-macro facts used in evaluation. A condition involving character literals can depend on character interpretation or signedness even though it contains no macro identifiers. GCC explicitly interprets preprocessing character constants according to the target’s character semantics. [GCC preprocessing conditions](https://gcc.gnu.org/onlinedocs/cpp/If.html)

**D. Blocker specification gap: Clang must evaluate the abstract state, not its ambient predefined environment.**

For example:

```cpp
#undef __clang__
#if defined(__clang__)
int main() { return 1; }
#else
int main() { return 0; }
#endif
```

The abstract state says `__clang__` is undefined. A fresh Clang preprocessor receiving only “the known definitions of the closure” still has its own predefined `__clang__`, unless explicitly isolated or overridden.

That evaluates the wrong branch. Usually the preprocess check rejects the result; it is still a defective evaluator, and finding B prevents relying on that check universally.

The closure algorithm itself is a reasonable conservative approach **provided the environment is exact**. Pasting rejection closes the obvious mechanism for manufacturing identifiers outside the closure. Overapproximating identifiers generally costs resolution opportunities rather than correctness.

Specify these details before implementation:

- Known-undefined names are genuinely undefined in the synthetic environment.
- Defined-with-unknown-definition names used only by `defined` receive a safe definedness representation.
- Function macro parameters are bound parameters, not external macro dependencies. Otherwise `#define F(x) (x)` unnecessarily introduces unknown `x`.
- Definitions used only as operands of `defined` need not have their replacement lists expanded.
- The evaluator receives the relevant dialect, arithmetic and character settings; absent knowledge means unknown.
- An unknown conditional without `#else` includes the unchanged entry state as an exit path.
- Each alternative starts from the proper entry state; definitions in a preceding, untaken branch must not leak into `#elif` evaluation.

For instance, after:

```cpp
#if defined(__clang__)
#define SWITCH 1
#endif
```

with unknown `__clang__`, `SWITCH` is not definitely defined. “All branches” must explicitly include the implicit no-op alternative.

**E. Major: A3 justifies ordinary guard absence, but not every other initial absence.**

The new rule for a non-reserved macro actually defined by user code is sound under A3. This resolves the important no-profile include-guard problem.

The separate profile rule—every other non-reserved name outside the facts starts undefined—requires more than a positive allow-list.

Suppose a supported compiler predefines a non-reserved `acme_target`, omitted from that list:

```cpp
#ifdef acme_target
int main() { return 1; }
#else
int main() { return 0; }
#endif
```

A3 does not apply: user code never defines it. A1–A2 concern headers. Accurate listed profile facts do not, by themselves, establish absence of an omitted compiler macro.

I am not claiming that the eventual lists omit a particular real macro; those lists do not yet exist. The specification must establish their exhaustiveness. GCC documents that system-specific predefined names vary by target and can include non-reserved spellings. [GCC system-specific predefined macros](https://gcc.gnu.org/onlinedocs/gcc-12.1.0/cpp/System-specific-Predefined-Macros.html)

**Fix:** Make an exhaustive initial non-reserved macro inventory, including its configuration scope, part of the profile contract covered by A4. Otherwise leave unlisted names unknown. Explicitly detect conflicts between profile definitions and the A3 assumption.

**F. Blocker: raw-only mode still performs an unsafe recursive deletion.**

This input meets the textual recursive-guard predicate.

`main.cpp`:

```cpp
int count = 0;
#pragma push_macro("G")
#include "h.hpp"
int main() { return count; }
```

`h.hpp`:

```cpp
#ifndef G
#define G
#ifdef _WIN32
#pragma pop_macro("G")
#endif
#ifdef SECOND
int bump = ++count;
#else
#define SECOND
#include "h.hpp"
#endif
#endif
```

There is no user-text `#undef G` and no intervening system include.

On Linux, `G` remains defined and the recursive inclusion does nothing. On Windows, the first `pop_macro` restores its saved undefined state, so recursion enters the header and defines `bump`. A later pop on the empty stack leaves the current definition unchanged. GCC documents these stack semantics. [GCC macro-stack pragmas](https://gcc.gnu.org/onlinedocs/gcc-13.3.0/gcc/Push_002fPop-Macro-Pragmas.html)

Consequently:

- Original Windows program: exit status 1.
- Raw file after dropping the recursive inclusion: exit status 0.
- Linux raw preprocessing and runtime checks can all pass.

Disabling resolution and pruning does not disable this inliner transformation.

**Fix:** Prove the guard’s state across every intervening effect. For v1, rejecting recursive inclusion when macro-stack operations or `_Pragma` could interfere is sufficient. Also restrict reliance on A2 to guard names that A2 actually protects.

**G. Blocker: line preservation cannot depend only on the listed user observations.**

A system header can expose a call-site location without a user token or macro expanding to `__LINE__`.

For example, `locations.hpp`, supplied through a system include path:

```cpp
inline int location(int n = __builtin_LINE()) { return n; }
```

User source:

```cpp
#include <locations.hpp>
#include "padding.hpp"
int main() { return location(); }
```

Let `padding.hpp` contain several harmless declarations or blank lines. Inlining it changes the physical line of the call.

The user text contains neither location builtin. No system macro expands to `__LINE__`. Therefore the specified activation rule need not emit mappings. Nevertheless, the default argument observes the changed call-site line. Both versions compile, and `-E -P` token equality does not detect it.

**Fix:** Emit and maintain logical line mappings unconditionally. That is considerably easier to justify than proving the absence of every observation, especially through unavailable configurations and system declarations. It also works in `--no-prune` and raw-only paths.

**3. D3 problems**

**H. Blocker: the inert whitelist still uses configuration-dependent evidence.**

Without a profile:

```cpp
int count = 0;

#ifdef __clang__
using T = int;
#else
struct T {
    T() { ++count; }
};
#endif

T unused;
int main() { return count; }
```

The resolver retains the compiler conditional. Clang sees `unused` as a scalar with no initializer, so it qualifies for the whitelist and can be removed.

GCC sees a class object whose constructor increments `count`. Removing it changes the exit status from 1 to 0. Both compile; the static name invariant passes.

The same defect affects initializers:

```cpp
int count = 0;

#ifdef __clang__
#define INIT 0
#else
#define INIT (++count)
#endif

int unused = INIT;
int main() { return count; }
```

Clang’s expanded initializer is harmless; GCC’s is not. No token pasting is involved.

**Fix:** The whitelist must prove harmless initialization and destruction across every configuration the output supports. Its type classification and macro expansion cannot come solely from the active Clang AST.

A conservative first implementation can require directly established scalar types and configuration-independent initializer expressions. Unresolved aliases, conditional macro definitions and dependent types fall outside the whitelist unless independently proved safe.

The listed whitelist operations are otherwise a substantial improvement. The defect is the scope of the proof, not the decision to use a positive whitelist.

**I. Blocker: keeping an operator does not preserve the declarations that make lookup find it.**

```cpp
#include <utility>

namespace N {
    template<class T>
    int operator+(T, T) { return 7; }
}

using namespace N;

#ifdef __clang__
int main() { return 0; }
#else
template<class T>
constexpr bool addable = requires(T x) { x + x; };

int main() {
    return addable<std::pair<int, int>> ? 0 : 1;
}
#endif
```

The operator family is now correctly retained. But the using-directive:

- declares no indexed name;
- is not a root;
- is not referenced by the active `main`;
- has no specified dependency edge from the unparsed expression.

Removing it makes the GCC requirement false. Both versions compile.

**Fix:** Initially retain all namespace using-directives, or define sufficient lookup-dependency edges for every potentially active configuration. Retaining all such directives is the simpler first-release boundary.

The conditional-block rule is now clear about **text ownership**. Keeping inactive text verbatim does not establish that its semantic dependencies survive.

**J. Blocker: hidden references are not limited to user macros using `##`.**

Suppose a system header defines:

```cpp
#define CALL(x) chosen(x)
```

User code:

```cpp
#include <vendor.hpp>

int chosen(int) { return 7; }

#ifdef __clang__
int main() { return 0; }
#else
template<class T>
constexpr bool available = requires(T x) { CALL(x); };

int main() { return available<int> ? 0 : 1; }
#endif
```

The inactive user text contains `CALL`, not `chosen`. The active AST supplies no reference. There is no user macro using `##`.

If the name rule scans only retained user text, `chosen` can disappear. On GCC, the dependent requirement then becomes false, while compilation still succeeds.

Scanning the locally observed system macro bodies would fix this example, but is not a general solution: a target header or an inactive compiler branch can supply a different replacement list.

**Fix:** Define the dependency boundary for macro expansion in unparsed code. If a potentially active invocation has an unknown replacement list, conservatively retain the possible dependencies or disable pruning. A1 constrains macro names; it does not constrain replacement lists sufficiently to infer their references.

**K. Major: accepted external libraries introduce an unstated reachability assumption.**

The command model accepts `-l<lib>`, but pruning treats user-code reachability as if external callers were fully known.

A library can expose `run()` and contain a weak reference to `hook`, returning 0 when the hook is absent. User code can provide:

```cpp
extern "C" int run();
extern "C" int hook() { return 7; }

int main() { return run(); }
```

`hook` has no user-side reference and need not carry any rooted attribute. Removing it can change the result from 7 to 0 while linking still succeeds.

A1–A4 do not cover external-library callbacks or symbol discovery.

**Fix:** Reject or disable pruning for unsupported external-linkage mechanisms, or retain the externally visible definitions they may reach. Explicitly invoking libraries is not evidence that their bodies are available to the AST traversal.

**4. Priority and verdict**

**Before D2 coding starts, settle these semantic rules:**

1. Align A1–A4 with the initial-state and header-transition rules, especially the exemption for profile facts.
2. Separate external configuration requirements from source-derived macro states.
3. Specify an isolated synthetic preprocessing environment and complete branch joins.
4. Correct the validation claim and handle observable source locations.
5. Close recursive inlining across macro-stack effects, including raw-only mode.
6. Make line preservation unconditional, or supply an equally complete alternative.

These are specification corrections, not requests for a larger testing budget. **D2 is still blocked as currently specified.** Independent command-parser, process-runner and distribution work can proceed without waiting for them.

**Before D3:**

1. Make inertness independent of the one Clang configuration.
2. Preserve lookup-enabling declarations.
3. Define conservative behavior for unknown macro expansions in unparsed code.
4. Define the external-linkage boundary.

**Suitable to settle during implementation:** exact AST visitors, family ownership and union-find bookkeeping, range pinning, macro-parameter closure precision, extension-list generation, option propagation, and retention measurements. The never-removed operator policy is a defensible conservative starting point; I would not refine it before corpus measurements.

My answer would change with explicit specification amendments closing these holes and adversarial regressions asserting the expected artifact or rejection. Tests should include examples where both outputs compile and `--verify-run` is absent. An exact judge compiler strengthens validation, but the location/token-equality example shows that it does not, by itself, make the current checks sufficient.