<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Codex review D3-2: pruning amendment, draft v8 (2026-11-16)

Question: round 2 on draft v8 of §1.3 (A6, stipulated by the user), §4.7, §4.8, §4.9, §6; disposition of each D3-1 finding, new problems, verdict. Cited as *D3-2-n*. Answer verbatim:

**Not ready as written, but v8 resolves most of the ownership and rendering issues.** I accept A6 as stipulated and no longer require blanket retention of every user non-member function template. The remaining blockers concern system-macro expansion, the precise coverage of A6, and dependent lookup involving pasted names.

I reviewed the supplied design and primary standard/library sources. I have not compiled the examples or inspected D3 implementation code. “Resolved” below means resolved in the design, subject to acceptance tests.

| Finding | Disposition in v8 |
|---|---|
| **D3-1-1** | **Partly.** Parse configuration, provenance, options and fallback behavior are now specified. The removal justification still has the macro and lookup gaps below. |
| **D3-1-2** | **Resolved as an assumption.** A6 plus unconditional retention of `iter_swap` closes the original example. The registry’s scope and applicability to admitted extensions remain implementation blockers. |
| **D3-1-3** | **Resolved for the reported example.** Expansion callbacks explicitly cover synthesized predefined names. This does not cover all sources of differing expansion; see D3-2-1. |
| **D3-1-4** | **Partly.** The semantic-equivalence overclaim is withdrawn, but the assertion that user code expands to identical tokens remains false. |
| **D3-1-5** | **Partly.** Traversal categories and conservative fallback are substantially improved. “Visible from its context” still needs a sound definition for dependent ADL. |
| **D3-1-6** | **Resolved.** Local ownership, whole-record retention, shells, static members and previously omitted declaration kinds are addressed. |
| **D3-1-7** | **Resolved as a design requirement.** Conservative retention is accepted and useful attribution is required. Corpus measurement remains D3 work. |
| **D3-1-8** | **Resolved.** Physical offsets and provenance distinguish generated markers from pinning directives. |
| **D3-1-9** | **Resolved.** Complete declaration units, shared ownership and directive/macro boundaries are specified. |
| **D3-1-10** | **Resolved.** Directive comments receive different treatment, with preservation when reconstruction is unsafe. |
| **D3-1-11** | **Resolved.** An independent rendering invariant now checks retained token locations and directive preservation. |
| **D3-1-12** | **Resolved.** Checks and publication operate on the same completed bytes, including fallbacks. |
| **D3-1-13** | **Partly.** Reporting terminology and adversarial cases are improved, but §7 still assigns checker tests to D4. Their essential D3 subset must be explicit. |

**D3-2-1 — Blocker: system-header macros still bypass the expansion guard (§4.7).**

A2(b) does not imply identical expanded tokens. It constrains user names introduced by a system macro; it does not constrain numeric replacements subsequently consumed by a **user** pasting macro.

```cpp
#include <cstdio>

#define JOIN_I(a, b) a##b
#define JOIN(a, b) JOIN_I(a, b)

int pick8192(int) { return 0; }
int pick512(int)  { return 0; }

template<class T>
constexpr bool available = requires(T x) {
    JOIN(pick, BUFSIZ)(x);
};

int main() {
    return available<int> ? 1 : 0;
}
```

Assume a Linux/glibc checking configuration and a MinGW judge configuration. The inspected headers define `BUFSIZ` as `8192` and `512`, respectively. These are ordinary header macros, not compiler predefinitions. [glibc `stdio.h`](https://raw.githubusercontent.com/bminor/glibc/master/libio/stdio.h), [MinGW-w64 `stdio.h`](https://raw.githubusercontent.com/mingw-w64/mingw-w64/master/mingw-w64-headers/crt/stdio.h).

Under v8:

- Clang sees `pick8192`; nothing requires keeping `pick512`.
- `BUFSIZ` introduces no user name, so this satisfies A2(b).
- No predefined macro need expand, so the new guard does not stop pruning.
- Removing `pick512` leaves no occurrence detectable by the raw-token invariant.
- The Linux original and candidate return `1`. The MinGW original returns `1`, but the candidate returns `0`.

A6 is irrelevant: this lookup occurs in user code. The example also disproves the identical-token assertion independently of any AST implementation.

**Proposed wording:**

> A2(b) does not establish equality of expanded user tokens. In D3, a reachable user macro expansion involving `##` disables pruning unless every input to the paste, including transitive system-header macro expansions, is proved invariant across the allowed configurations. Clang-observed expansion alone is insufficient. Until that proof is implemented, use the disabling rule.

This conservative fallback is narrower than retaining every function template and directly addresses the unsafe mechanism.

**D3-2-2 — Major specification gap, blocking A6 implementation: the registry needs a defined domain (§1.3, §4.7).**

**Yes, A6 closes D3-1-2 as an assumption.** There is also a standard basis for restricting ordinary library ADL: `[global.functions]` generally prohibits standard-library calls from selecting non-operator, non-member functions from another namespace through ADL unless otherwise specified. `[contents]` separately defines how names in library specifications are interpreted. [C++23 `[global.functions]`](https://timsong-cpp.github.io/cppwp/n4950/global.functions), [C++23 `[contents]`](https://timsong-cpp.github.io/cppwp/n4950/contents).

However, “standard ADL / customization-point names …” is not yet a sufficiently precise manifest:

- It needs a specified standard edition and defect-report policy.
- It mixes **public CPO names** with **names actually looked up in user namespaces**.
- It omits language-generated lookup from its stated scope.
- It does not distinguish function-name customization from template specialization.

For C++20/23, the audit must cover at least these groups:

| Mechanism | Required coverage and clauses |
|---|---|
| Swapping and iterator customization | `swap`, `iter_move`, `iter_swap`: `[swappable.requirements]`, `[concept.swappable]`, `[iterator.cust.move]`, `[iterator.cust.swap]`. |
| Range access | ADL hooks `begin`, `end`, `rbegin`, `rend`, `size`: `[range.access]`, including `[range.prim.size]`. Range-for also needs `begin`/`end` under `[stmt.ranged]`. |
| Structured bindings | `get` under **language clause** `[dcl.struct.bind]`, whether the binding occurs in user or library code. |
| Error-code conversion | `make_error_code`, `make_error_condition`: `[contents]` and the corresponding `[syserr]` constructors/assignments. |
| Comparison customization | `strong_order`, `weak_order`, `partial_order`: `[cmp.alg]`. |
| Chrono parsing | `from_stream`: `[time.parse]`. |
| Valarray mathematical operations | `abs`, `acos`, `asin`, `atan`, `atan2`, `cos`, `cosh`, `exp`, `log`, `log10`, `pow`, `sin`, `sinh`, `sqrt`, `tan`, `tanh`: `[valarray.transcend]`. |

These are substantive distinctions. Structured bindings perform ADL for `get` without a user-spelled call; chrono explicitly calls `from_stream` unqualified; valarray explicitly requires an applicable unqualified mathematical function. [Structured bindings](https://timsong-cpp.github.io/cppwp/n4950/dcl.struct.bind), [chrono parsing](https://eel.is/c++draft/time.parse), [valarray transcendentals](https://eel.is/c++draft/valarray.transcend).

Conversely, free `cbegin`, `cend`, `crbegin`, `crend`, `ssize`, `empty`, `data` and `cdata` are not all independent ADL hooks merely because corresponding range CPOs exist. Keeping those names is harmless overretention. The registry should record that distinction rather than imply that each CPO calls a same-named user function. [C++23 range access](https://timsong-cpp.github.io/cppwp/n4950/range.access).

**`std::hash` belongs to a separate mechanism.** Its customization is a class-template specialization, whose call operator is a member. Adding `hash` to a free-function-name list does not establish preservation. The same audit must consider permitted specializations such as `formatter`, `tuple_size`, `tuple_element`, iterator traits, and range customization variable templates. Their retention must follow specialization identity and conservative association, independently of which specialization Clang selected. V8’s separate specialization rules are the right approach; make their coverage explicit. [Hash specializations](https://eel.is/c++draft/unord.hash), [namespace `std` specialization rules](https://eel.is/c++draft/namespace.std).

Also distinguish lookup from callbacks: `std::thread(work)` or an algorithm receiving a function pointer can invoke a function with an arbitrary name. Its retention follows the supplied callable’s dependencies, not an ADL-name registry.

**Proposed wording:**

> The registry is versioned by supported dialect and records the lookup mechanism and normative clause for each hook name. It covers language-generated lookup as well as library lookup, including unevaluated probes and constraints. Public CPO names and their underlying customization names are distinguished. Callback invocation, member customization and permitted template specializations are covered by separate retention rules. Unsupported dialects or mechanisms disable pruning.

Later dialects need a fresh audit; extending a C++23 manifest with an ellipsis is insufficient.

**D3-2-3 — Major compatibility problem: admitted GNU extensions have additional ADL surfaces (§1.3, §4.7).**

**Yes, there are concrete non-standard-hook unqualified calls in libstdc++ in practice.**

GCC 14.2’s `<ext/rope>` implementation contains unqualified:

- `copy_n` in `_S_flatten`;
- `uninitialized_copy_n` in leaf concatenation and flattening support.

The arguments include pointers to the template’s character type, so a user-defined character type supplies associated namespaces. An appropriately matching user overload can participate. Neither name is a standard customization hook for those algorithms. [GCC 14.2 `ropeimpl.h`](https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-14.2.0/libstdc++-v3/include/ext/ropeimpl.h).

For example, the `_S_flatten` call can consider a non-template overload of this shape when the rope’s character type is `app::Ch`:

```cpp
namespace app {
enum Ch { letter };

std::pair<Ch*, Ch*>
copy_n(Ch* first, std::size_t count, Ch* output);
}
```

That is source-level evidence of an additional lookup surface, not a claim that this particular overload escapes v8’s association rule.

Valarray’s unqualified mathematical calls are another real libstdc++ surface, but those **do** belong in the standard-derived registry described above. Its implementation explicitly uses unqualified `abs`, `cos`, and other mathematical names. [libstdc++ `valarray_before.h`](https://gcc.gnu.org/onlinedocs/gcc-12.1.0/libstdc%2B%2B/api/a00776_source.html).

Under stipulated A6, a non-listed extension call is outside the assumption’s domain; it is not a logical counterexample satisfying A6. It does mean the design cannot claim that A6 accurately describes every header currently admitted by its standard-header list.

This matters to GNU MinGW toolchains using libstdc++ too. I have **not** established an additional MinGW CRT-specific non-listed ADL hook, nor completed an exhaustive audit of its headers.

**Proposed wording:**

> Pruning eligibility requires both preprocessing-header classification and a supported customization-surface classification. GNU extension headers are not certified by the standard-only A6 registry. Until their additional lookup names are audited and covered, their inclusion disables pruning.

This preserves the chosen finite-name strategy without silently assuming standard-library guarantees apply to `<ext/rope>` or PBDS.

**D3-2-4 — Blocker under the narrow reading: dependent lookup must not be limited to currently visible candidates (§4.7).**

A6 concerns library lookup. Arbitrary user-to-user dependent calls still require conservative treatment.

```cpp
#define JOIN_I(a, b) a##b
#define JOIN(a, b) JOIN_I(a, b)

namespace app {
struct X { int payload[2]; };
}

template<class T>
int run(T x) {
    if constexpr (requires { JOIN(custo, mize)(x); })
        return JOIN(custo, mize)(x);
    else
        return 0;
}

namespace app {
template<class T>
    requires (sizeof(long) == 4 || sizeof(T) == 1)
int customize(T) {
    return 1;
}
}

int main() {
    return run(app::X{});
}
```

Assume the same LP64/LLP64 distinction as round 1:

- `customize` is declared after the template definition and is found through ADL at instantiation.
- Its constraint rejects `app::X` on LP64 and accepts it on LLP64.
- Its signature names no user record.
- The full identifier `customize` is absent from retained raw text.
- The pasted name is identical on both targets; no differing predefined macro is involved.

A traversal restricted to definition-context visibility or successful Clang selections can remove it. The judge then takes the valid fallback and returns `0` instead of `1`. Dependent ADL includes instantiation-context declarations; definition-context ordinary lookup is insufficient. [Dependent candidate lookup](https://eel.is/c++draft/temp.dep.candidate).

“Visible from its context” might have been intended to include a broader analysis. That ambiguity is precisely what needs removing before implementation.

**Proposed wording:**

> For every dependent call name in a kept user template pattern, retain every user non-member family with that name throughout the translation unit, including hidden friends and later declarations. Do not filter by Clang viability or observed instantiation. Include names recovered from supported macro expansion; otherwise disable pruning.

This retains matching names, not all generic functions. D3-2-1’s blanket reachable-pasting fallback also safely rejects this example.

**D3-2-5 — Major: comment stripping can erase explicit keep requests before pruning (§4.7–§4.8).**

V8 makes comment stripping part of rendering every checked artifact, while D3 consumes checked `flat` bytes. Its listed provenance does not include keep annotations.

```cpp
// distill: keep
int preserve_me() { return 7; }

int main() { return 0; }
```

With `--strip-comments`, D3 can receive a comment-free flat buffer and lose the root. Compilation, runtime comparison and both invariants can all pass.

**Proposed wording:**

> Extract keep annotations before comment stripping and preserve their declaration/physical-occurrence association in transformation provenance. Pruning consumes this metadata independently of whether the annotation comment survives rendering.

Add a test requiring the function to survive while its comment disappears.

**D3-2-6 — Major: essential checker tests must gate D3 (§6–§7).**

The new §6 cases are appropriate, but §7 still puts “checker tests” in D4. D3 must establish the behavior of the rules it implements, including deliberate examples that the checks cannot detect.

**Proposed wording:**

> D3 completion requires the pruning adversarial suite and checker-limit tests. The test runner performs required execution comparisons independently of the later public `--verify-run` feature. D4 adds CLI integration and broader coverage.

Add acceptance cases for:

- `BUFSIZ`-driven pasting and later-declared dependent ADL;
- structured-binding `get`, comparison hooks, error conversion, chrono parsing and valarray hooks;
- specialization-based customization, including permitted variable-template specializations;
- refusal or explicit coverage for GNU extension lookup;
- `--strip-comments` combined with keep annotations.

For cross-target cases, assert retention or refusal directly. Passing local executions cannot establish that result.

**D3-2-7 — Minor: report the actual pruning policy and registry (§4.9).**

Reporting “A6” alone does not identify which finite list justified a removal. That becomes important when dialects or extension coverage change.

**Proposed wording:**

> Report the pruning-policy version, customization-registry version or digest, selected dialect coverage, and applicable library-extension coverage. Record A6 as a dependency of negative removal reasoning even when no ADL-name root was encountered.

One small terminology correction also belongs in §4.7: say **“static or thread storage duration”**. `thread_local` has thread storage duration, although the parenthetical currently communicates the intended conservative treatment.

**Verdict:** D3 can proceed after the macro-pasting fallback, dependent-name closure, concrete versioned A6 registry and extension-eligibility rule are specified, with keep-annotation provenance and the essential tests assigned to D3. The reporting amendments can land with that work.

I would change this verdict with concrete rules and acceptance evidence covering those cases. Matching compiler probes, additional ordinary samples, or a `target` label would not close them. The original broad function-template retention proposal is no longer a prerequisite under the stipulated A6.