<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Codex review D3-1: pruning rules against v7 and D2 (2026-11-16)

Question: adversarial review of §4.7 Pruning, §4.8 Rendering, §4.9 Report, the `pruned` row of §4.6 and the §6 tests, against approved v7 and what D2 implemented (full design and `src/core/README.md` supplied). Cited as *D3-1-n*. Answer verbatim:

**Not ready for D3 as written.** The central problem is that “satisfies the resolution” is a preprocessing property, while pruning relies on semantic properties that can differ between Clang, the checking compiler, and the judge. The predefined-macro guard does not close that gap, and token pasting can bypass the guard itself.

I treat the supplied D2 notes as the implementation contract; I have not inspected implementation source or executed the examples. The compiler-specific AST behavior below should therefore become an acceptance test against the pinned LLVM build. I distinguish that uncertainty from defects evident directly from the rules.

**1. Blocker — the pruning configuration and its proof obligations are not defined sufficiently**

D2 establishes a useful, narrow fact:

> The checking configuration reproduces the preprocessing decisions used to construct `flat.cpp`.

It does **not** establish any of these:

- Clang and the judge give `long`, `wchar_t`, or library types the same representation.
- They select the same constrained overloads or template specializations.
- Their standard headers expose the same declarations, defaults, constraints, or customization paths.
- Clang’s builtin expressions and the judge compiler’s builtin expressions agree.
- Absence of a dependency in Clang’s instantiated AST implies absence on the judge.

A condition-free program satisfies D2’s resolution test essentially vacuously. It can still depend heavily on those differences.

The following interfaces need explicit decisions before implementation:

| Location | Missing specification | Required clarification |
|---|---|---|
| §4.2 → §4.7 | Which compiler supplies Clang’s probe? | Use the **selected flat checking configuration**, identified explicitly. Do not silently use the baseline compiler. |
| §4.2 | Which target does Clang use when flat uses a local fallback? | Its target, headers and ABI options must form one coherent configuration. A Windows triple with Linux libstdc++/glibc paths is not a judge parse. |
| §4.1–4.3 | Which options reach Clang? | Derive them from D2’s effective configuration after profile overrides, including dialect, macro operations, ABI flags and relevant semantic flags. Record every translation or omission. |
| §4.2 | Clang retains its own macros, but whose command macros apply? | Profile operations replace local `-D`/`-U`, exactly as for flat. Define their order relative to Clang predefinitions. |
| §4.7 | What constitutes the “judge predefined set”? | Use effective, provenance-aware facts, including special builtins and command operations. An approximate snapshot’s withheld definitions are not established judge facts. |
| §4.7 | What if a source `#undef` or `#define` changes a predefined name? | Compare the binding at the use site, not merely the initial dumps. |
| §4.7 | What is the pruning input? | The accepted flat bytes **and their D2 provenance map, directive inventory and disabling flags**. |
| §4.7 | Is `unknownInUserCode` authoritative? | Yes. Do not clear it because the local Clang parse happens to select one branch. Generated guards count too. |
| §4.7 | What happens after parse errors, unsupported constructs or limits? | Reject pruning and retain checked flat. Never analyze an error-recovery AST as complete. |
| §4.6 | What does `target` mean for pruning? | Matching the judge’s preprocessing environment is a validation label, not proof of Clang semantic equivalence. |
| §4.6 | What additional facts does pruning require? | Record them separately from resolution facts; the current satisfaction predicate cannot certify them. |
| §4.6 | Which baseline supplies runtime comparison? | Flat built in the same checking configuration, with identical execution conditions. D2 currently supplies no runtime comparisons. |

D2 also models `-fmodules` and `-fopenmp`, unlike the option table presented in §4.1. Those need explicit D3 handling. Profile options classified as preprocessing-inert can still affect semantic analysis; `-fconstexpr-depth` is an obvious example.

**Proposed wording:**

> Construct and report a distinct `ParseConfiguration` from the selected flat validation configuration. It records Clang’s version, target, resource headers, system-header search order, dialect, effective macro operations, semantic options and unsupported translations. Resolution satisfaction alone does not authorize pruning. Each removal must remain justified under the judge configurations allowed by the recorded facts; otherwise retain the affected families or disable pruning.

Using the checking configuration is the least surprising implementation choice. It does **not**, by itself, fix soundness.

**2. Blocker — target-dependent ADL can remove a judge-required function**

Here is a concrete C++20 example:

```cpp
#include <algorithm>
#include <cstddef>
#include <iterator>

namespace app {
int calls = 0;

struct It {
    using value_type = int;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::bidirectional_iterator_tag;

    int* p;

    int& operator*() const { return *p; }

    It& operator++() { ++p; return *this; }
    It operator++(int) {
        It old = *this;
        ++*this;
        return old;
    }

    It& operator--() { --p; return *this; }
    It operator--(int) {
        It old = *this;
        --*this;
        return old;
    }

    friend bool operator==(It, It) = default;
};

template<class T>
    requires (sizeof(long) == 4 || sizeof(T) == 1)
void iter_swap(T a, T b) {
    int temporary = *a;
    *a = *b;
    *b = temporary;
    ++calls;
}
}

int main() {
    int a[] = {1, 2};
    std::ranges::reverse(app::It{a}, app::It{a + 2});
    return app::calls;
}
```

Assume Clang and the checking compiler use x86-64 Linux, while the judge uses x86-64 MinGW:

- On Linux, `sizeof(long) == 8` and `sizeof(It) == 8`. The custom `iter_swap` is not viable. The library swaps the referenced integers through its fallback.
- On MinGW, `sizeof(long) == 4`. ADL finds the custom function; the original returns `1`.
- Removing the custom function leaves a valid fallback on MinGW, so the pruned program builds and returns `0`.

The specified retention rules do not require keeping that function:

- Its indexed name does not occur in retained **user** text.
- Its signature names `T`, not `It`.
- `iter_swap` is absent from the never-remove list.
- A successful local call expression need not reference a rejected ADL candidate.
- The uninstantiated system template does not contain a completed set of future ADL candidates.

Thus the static invariant passes, local builds pass, and **every local runtime comparison passes**, regardless of sample coverage.

The standard requires ranges reverse to use `ranges::iter_swap`, whose customization mechanism includes ADL. [Reverse specification](https://eel.is/c++draft/alg.reverse), [iterator swap customization](https://eel.is/c++draft/iterator.cust.swap).

An implementation that additionally retains all rejected ADL candidates might retain this particular function. That is not a guarantee supplied by §4.7, and it still would not enumerate candidates for arbitrary different judge instantiations.

**Proposed wording:**

> In D3, retain every user non-member function template, including abbreviated templates and hidden friends, unless a separate conservative rule proves that it cannot participate in a retained operation under any supported judge configuration. The named customization-function list is not exhaustive.

This is deliberately expensive. Adding only `iter_swap` fixes this example, not the underlying argument. Correspondingly, root user specializations of system templates until their possible selection is covered conservatively.

**3. Blocker — token pasting bypasses the predefined-macro guard**

Consider a GCC 15 checking compiler and judge, with Clang’s usual GCC-compatibility value of `__GNUC__ == 4`:

```cpp
#include <cstdio>

#define CAT_IMPL(a, b) a##b
#define CAT(a, b) CAT_IMPL(a, b)

int pick4(int)  { return 0; }
int pick15(int) { return 0; }

template<class T>
constexpr bool available = requires(T value) {
    CAT(pick, CAT(__GNUC, __))(value);
};

int main() {
    int c = std::getchar();
    return c == 'x' ? available<int> : 0;
}
```

The spelling `__GNUC__` does not occur as an identifier token in the source. It is synthesized from `__GNUC` and `__`, then expanded and pasted into a function name.

Under the literal guard:

- Neither fragment is the predefined macro being compared.
- Clang sees `pick4`, retains it through AST references, and may remove `pick15`.
- The final raw text contains no `pick15` identifier, so the static invariant passes.
- GCC still builds the candidate: the dependent requirement becomes false when `pick15` is absent.
- A sample containing `y` passes.
- With input `x`, the original returns `1`; the candidate returns `0`.

This can happen even when the checking configuration deserves the current **`target`** label.

This example assumes an exhaustive profile in which the two fragments are known absent. Treating every identifier with unknown predefined status as a reason to stop might block it for an approximate profile, but does not fix the exhaustive-profile case.

**Proposed wording:**

> Predefined-macro use detection includes names synthesized during expansion and rescanning, not just identifiers spelled in the input. Until this is implemented conservatively, user token-pasting macro definitions disable pruning.

A later refinement can restrict that fallback to reachable uses. Expansion callbacks and token recording can help, but recording only the Clang expansion cannot establish the judge’s expansion.

Also define whether the invariant examines raw or expanded tokens. Its present “without a compiler” description implies raw tokens; it must not be credited with detecting pasted names.

**4. Major — A1–A5 do not establish the claimed token or semantic equivalence**

The sentence beginning “Under these two conditions…” overclaims what the assumptions establish.

A2(b) protects a particular **name-preservation property** of system macros. It does not make system macro replacements identical. A1 bounds names that headers define; it does not make their declarations, layouts or overload sets identical.

For example, a constrained customization can depend on:

```cpp
requires (sizeof(std::string) == 32)
```

Two library ABIs can make that condition differ with no user conditional and no direct predefined-macro use. This can replace the target-width condition in finding 2 when the selected library layouts differ.

Likewise, `_WIN32` can select declarations **inside system headers**, even though no unknown user conditional remains. MinGW and glibc libstdc++ environments need not expose identical overloads or type aliases. Clang can also select different header branches from GCC when both read the same installation.

**Proposed wording:**

> A1–A5 justify the stated preprocessing assumptions only. They do not establish equality of library declarations, layouts, overload resolution or template instantiations. System-header AST edges are positive retention evidence; absence of an edge is not judge-independent evidence that a user declaration is unused.

Matching targets and headers reduces uncertainty. A stronger, explicit trust assumption or conservative retention is still necessary; a successful parse is insufficient.

**5. Major — AST traversal needs a coverage specification, not “every declaration referenced”**

Several parts are implementable, but the difficult cases are exactly those for which an AST reference graph is incomplete.

| Case and example | What Clang supplies | Simplest conservative treatment |
|---|---|---|
| `template<class T> void f(T t) { customize(t); }` | Dependent lookup information, not all future ADL candidates | Retain matching spelled names; conservatively root generic customization candidates as above. |
| `requires { typename T::type; helper(t); }` | Requirement nodes and substitution-failure representations | Scan the complete retained lexical range too; do not rely on successful instantiated requirements alone. |
| `requires { CAT(helper, 1)(t); }` | Expansion/AST information for the parse environment | Apply the token-pasting fallback; raw name scanning alone is insufficient. |
| `MAKE_CLASS(Node)` | Declarations whose spelling and expansion locations differ | Pin the whole invocation and all declarations it produces; retain their dependencies. |
| `std::unordered_map<Key, int>` with user `std::hash<Key>` | Implicit template selection and instantiated bodies | Preserve the user specialization’s identity before mapping instantiations to primaries. |
| `asm("...symbol...")` or alias attributes | Strings and some structured attributes | Interpret only supported forms; disable pruning for opaque symbol references. |
| Constructors, destructors, conversions, default arguments, bases and constraints | Multiple AST node kinds, not just `DeclRefExpr` | Specify and test those edge categories explicitly. |

`UnresolvedLookupExpr::requiresADL()` signals that ADL is required; it does not provide a portable list of all future candidates. Clang’s recursive visitor also defaults to skipping template instantiations and implicit code. [Unresolved lookup API](https://clang.llvm.org/doxygen/classclang_1_1UnresolvedLookupExpr.html), [visitor implementation](https://github.com/llvm/llvm-project/blob/main/clang/include/clang/AST/RecursiveASTVisitor.h).

The phrase “unparsed `requires`” needs care: ordinary template patterns are generally parsed. Failed substitution, deferred lookup and discarded semantic representations are distinct problems. The design should not depend on an assumption that every lexical name survives as a usable AST edge.

**Proposed wording:**

> Enumerate supported dependency-bearing AST constructs. Traverse patterns, known instantiations, implicit operations, types, defaults and constraints. Keep lexical dependencies independently. An unsupported construct or incomplete dependency analysis pins its owning family; if its dependencies cannot be bounded, pruning is disabled.

The online API references are current documentation, not verification of the exact LLVM 23 build. Pin-specific tests remain necessary.

**6. Major — family ownership and root scope are ambiguous**

Several literal readings make pruning ineffective, while alternative readings can lose declarations.

Examples:

```cpp
void unused() {
    int local = 0;
}
```

“Every variable not on the inert whitelist” includes `local` literally, because the whitelist requires namespace scope. Making it a root retains the otherwise unused function.

```cpp
struct Unused {
    void method() {}
};
```

“Never removed: record members” must mean members of a **retained** record. Otherwise every member roots every record.

```cpp
namespace library {
    void unused() {}
}
```

“Namespaces never removed” must preserve the namespace shell, not classify its entire contents as never-removed text.

There are also missing or unclear owners: concepts, structured bindings, namespace-scope `static_assert`, explicit instantiation declarations/definitions, linkage blocks, anonymous entities and friend definitions.

For example:

```cpp
template<class T>
concept Small = sizeof(T) <= 8;
```

Concept names are missing from the explicit indexed-name list.

**Proposed wording:**

> Every user token belongs to a retained structural region or an explicitly supported removable family. Unrecognized declarations are retained with their dependencies. Record retention includes all lexical contents, including friends, nested declarations and member definitions. Namespace and linkage shells do not root their children. Local declarations belong to their enclosing family and are not independently rooted merely for failing the namespace-variable whitelist.

Treat static data-member definitions separately: a non-inert static member can have initialization effects even when the record is otherwise unused. Do not “fix” local-variable overretention by excluding all members from initialization roots.

**7. Major for usefulness — the current rules can retain most of a normal CP library**

These are not speculative optimizer corner cases:

| Library pattern | Retention consequence |
|---|---|
| Non-member `operator+`, `operator*`, stream operators for `Mint` | Operator roots retain `Mint`; whole-record retention retains its implementation; association then retains additional functions mentioning it. |
| Generic operators with constraints | Rooted regardless of whether any actual operand type uses them. |
| `inline constexpr auto op = [](auto a, auto b) { ... };` | The closure variable is class-type and therefore a root. It retains its body’s dependencies even if no segment tree uses it. |
| `using namespace std;` plus unqualified-name indexing | The directive itself is small, but common tokens such as `size`, `begin`, `data` and `get` retain unrelated user families with those names. |
| `using library::SegmentTree;` | The never-removed using-declaration spells the class name and retains it even when unused. |
| A retained `Key` and `std::hash<Key>` | Association retains the hash specialization even if no hash container is used. |
| A retained class template | The full primary, specializations and member bodies remain, including unused algorithms and helper dependencies. |
| Macro definitions mentioning optional helpers | Definitions are never removed, so the name rule roots helpers even if the macro is never invoked. |

A typical operator-heavy number library can therefore survive nearly intact. That is a legitimate correctness-first outcome, but it needs to be measured and described honestly.

**Proposed wording:**

> D3 permits conservative retention of most or all input code. Report both the initial retention cause and transitive retained bytes. Corpus measurements must separately identify operator roots, namespace using declarations, class-type globals, generic-function roots, name collisions and family aggregation.

Do not weaken these rules merely to meet a size target. First resolve the soundness issues and the unintended root interpretations in finding 6.

**8. Major — D2’s generated `#line` markers conflict with directive pinning**

D2 can produce the equivalent of:

```cpp
int unused() {
#line 40 "library.hpp"
    return 0;
#line 44 "library.hpp"
}
```

The current rule pins a family crossing a directive. Applied literally, this function cannot be removed. Resolved conditionals inside library functions will cause this frequently.

The alternative—treating presumed locations as editable locations—is worse. Repeated inlining can produce several physical occurrences with the same original file and line.

**Proposed wording:**

> Edit ranges use physical byte offsets in the immutable flat buffer. Original and presumed locations are for provenance and reporting only. Generated `#line` directives are rendering metadata, not user directives that pin families. Their effects are reconstructed from the D2 provenance map after deletion.

Clang explicitly distinguishes presumed locations affected by `#line` from physical locations. [SourceManager documentation](https://clang.llvm.org/doxygen/classclang_1_1SourceManager.html).

Do not infer marker provenance solely from its spelling. Carry D2’s generated/source distinction into D3.

**9. Major — deletion units need explicit ownership and boundary rules**

Clang declaration ranges are not automatically safe deletion ranges.

Examples:

```cpp
int unused, needed;
struct S {} object;
template<class T> struct Box {};
extern "C" { int hook(); }
```

The renderer must account for shared declarators, semicolons, template prefixes and enclosing structures. An out-of-line member also needs the correct family even when its spelling is macro-assisted.

This example must remain pinned under the current first-version policy:

```cpp
int unused() {
#define ANSWER 42
    return 0;
}
int main() { return ANSWER; }
```

The macro’s effect escapes the C++ scope. Deleting the whole function range would remove something needed later.

**Proposed wording:**

> Compute deletion ownership for complete syntactic declarations, including required terminators and prefixes. Shared ownership merges retention obligations. A physical token covered by multiple obligations is deleted only when all owners are removable. A range crossing a source directive or sharing a macro invocation with retained text pins all affected owners.

For D3, pinning the entire function containing `ANSWER` is simpler than extracting its directive.

Clang’s `Lexer::makeFileCharRange` can reject or map certain macro ranges, but successful conversion does not prove that the range is a complete deletion unit. [Lexer documentation](https://clang.llvm.org/doxygen/classclang_1_1Lexer.html).

Also clarify partial-line deletion: remove the owned tokens even when other declarations remain on the line; retain safe separators and reconstruct locations.

**10. Blocker when enabled — `--strip-comments` can change macro semantics while all stated checks pass**

Replacing a multiline comment with its newlines is not generally preprocessing-equivalent.

```cpp
#include <cstdio>

int main() {
#define VALUE 1 /* comment
*/ + 2;
    if (std::getchar() == 'x')
        return VALUE;
    return 0;
}
```

The original macro replacement is `1 + 2;`. Its original execution returns `3` for `x`.

Replacing the comment with a newline produces:

```cpp
#define VALUE 1
 + 2;
```

Inside `main`, `+2;` is a valid expression statement. The candidate builds but returns `1` for `x`. A sample containing `y` passes, and the static name invariant detects nothing.

This is precisely the multiline-comment/directive distinction that D2’s lexer notes already recognize.

**Proposed wording:**

> Comment stripping preserves preprocessing-token spellings, directive boundaries and required logical locations. It must not replace a newline inside a comment with an exposed directive-terminating newline. Where safe reconstruction is unavailable, preserve the comment or decline stripping.

“Check the exact bytes” remains necessary, but does not prove equivalence. This example shows why.

**11. Major — line preservation needs an independent rendering invariant**

Consider:

```cpp
#include <cstdio>

int unused() { return 0; }

int main() {
    return std::getchar() == 'x' ? __LINE__ : 0;
}
```

Deleting the unused declaration without correct resynchronization changes the result for `x`. Builds and the name invariant pass; a sample containing `y` passes.

The renderer should independently verify the location mapping of retained source tokens. Its difficult cases include:

- Several declarations on one physical line.
- Multiline comments and raw strings.
- Escaped newlines and CRLF.
- Repeated inlined occurrences.
- Markers adjacent to directives and at EOF.
- Default arguments using `__builtin_LINE`.
- A surviving system include whose default arguments observe the logical call location.

**Proposed wording:**

> After rendering, verify that every surviving source token has its intended logical file and line, and that retained source directives preserve their token sequence and order. Insert markers only at lexically safe boundaries. Failure rejects the candidate.

This is not a request for full flat/pruned preprocessor equality: intentional declaration deletion makes that impossible.

**12. Major — exact-byte validation must cover every output variant and fallback**

D2 already has a good foundation: the pipeline returns checked bytes, and the CLI publishes them without rendering again. Preserve that architecture.

The open cases are comment stripping, generated markers, output preambles, fallback selection and stdout publication.

**Proposed wording:**

> Finish all transformations, including comment stripping and line-marker reconstruction, before validating an artifact. Build, run, and evaluate the static/rendering invariants against that immutable byte buffer. Publish that buffer without modification. A fallback is eligible only in its own checked byte form.

For example, after candidate failure, the implementation must not take checked flat and then strip its comments before writing it.

For file output, stage and replace only after validation. For stdout, finish validation before emitting any artifact bytes.

**13. Major — the report and D3 tests must expose these limits**

“Unreachable” is generally an unjustified removal reason. No current rule proves semantic unreachability.

The report should distinguish:

- Parse configuration from checking configuration.
- Resolution facts from pruning assumptions.
- “No retention rule reached this family” from proven dead code.
- Primary reasons from transitive causes.
- Unique retained bytes from overlapping rule attribution.
- A failed candidate’s analysis from the certification of selected flat.
- Runtime comparisons actually performed from D2’s current `not requested`.

**Proposed wording:**

> Report removal as “removed: outside conservative retention closure,” with the applicable policy and analysis configuration. Report all pruning-disable reasons and uncertainties. Verification level describes the validation environment; it does not assert exhaustive runtime equivalence.

The critical adversarial tests belong in **D3**, even if general `--verify-run` support remains D4:

1. The LP64/LLP64 ADL example: assert retention or pruning refusal, not merely successful local execution.
2. The pasted-predefined example: assert that the guard detects or conservatively rejects it.
3. Multiline-comment stripping inside a macro: verify token/directive equivalence.
4. Generated `#line` inside an otherwise removable declaration.
5. A real `#define` inside a removable-looking function.
6. Local variables versus independently initialized static-storage variables.
7. Concepts, hidden friends, explicit instantiations and system-template specializations.
8. Forced bad removals with valid builds and matching samples, explicitly demonstrating checker limitations.
9. Corpus cases covering Mint, lambdas, segment trees, hash specializations and namespace imports.

Synthetic standard-header fixtures should stay within A1–A5 when testing the assumptions’ sufficiency. Tests deliberately violating an assumption should be labeled separately.

**Verdict:** D3 can proceed after the design defines the parse configuration, replaces the semantic-equivalence claim with conservative retention obligations, closes the token-pasting hole, resolves family/root ownership, and specifies safe rendering and comment stripping. These fixes should land with adversarial D3 acceptance tests.

I would reconsider the broad generic-function retention recommendation if a narrower rule came with a convincing argument covering target-dependent ADL, system-template instantiation and header differences. Matching macro dumps, adding more sample runs, or obtaining `target` verification would not change that recommendation.