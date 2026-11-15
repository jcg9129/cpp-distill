**D2 can proceed using the specified conservative behavior while A2 remains pending. D3 is still blocked.** Removing unknown user conditionals closes the original inactive-branch examples, but does not establish §4.7’s claim that Clang analyses exactly what GCC compiles: ordinary macro expansion and template selection can still differ.

I accept §1.1 and A1–A4 as directed. I assume defined behavior in the judge configuration, C++20 or later for the examples, and no `--verify-run`. These are reasoned counterexamples; I have not executed them against an implementation.

| R4 item | Status | Assessment |
|---|---|---|
| **A — Header changes to predefined macros** | **Resolved** | Preservation is now explicitly conditional on the A2 extension; without it, §4.5 invalidates predefined macros. Use that conservative path while approval is pending. |
| **B — Location-insensitive preprocessing comparison** | **Resolved** | Logical token locations and pragma boundaries address the example. The check is correctly described as a check rather than a proof. |
| **C — External versus derived macro states** | **Resolved** | Replaying resolution with the validating configuration’s initial requirements correctly handles source definitions and repeated guards. |
| **D — Synthetic environment and branch joins** | **Resolved** | Isolation, definedness placeholders, bound parameters and the implicit empty alternative are specified. Implementation must honor the promised isolation. |
| **E — Unjustified initial absence** | **Resolved under A4** | The exhaustive inventory supplies negative facts; profiles without one conservatively leave other names unknown. |
| **F — Recursive inclusion across macro stacks** | **Resolved** | The rejection applies during inlining, including raw-only operation, and guards must be protected by A2. |
| **G — Conditional line preservation** | **Resolved** | Unconditional mappings cover observations introduced by system declarations. |
| **H — Configuration-dependent inertness** | **Partly** | The original user-`#if` examples are closed. Template-selected aliases can still give Clang and GCC different types without any conditional directive. |
| **I — Lookup-enabling declarations** | **Resolved** | Retaining namespace-scope using-directives and using-declarations closes the reported hole. |
| **J — Hidden macro dependencies** | **Partly** | Arbitrary system headers disable pruning, but compiler-dependent macro expansion still defeats the new gate. Without the A2 extension, standard-header macros retain another uncovered route. |
| **K — External library reachability** | **Resolved** | Unsupported libraries disable pruning, and retaining `extern "C"` definitions covers the reported hook. |

The following counterexamples exercise v5’s new boundary; none relies on an unknown conditional remaining in user code.

**1. D3 blocker, with or without the A2 extension: ordinary macro expansion can select an unseen dependency.**

Take GCC 14 as the judge/compiler configuration and the bundled Clang with its usual GCC-compatibility macros. Clang documents a default compatibility version of 4.2.1; GCC’s `__GNUC__` reports its own major version. [Clang options](https://clang.llvm.org/docs/ClangCommandLineReference.html#cmdoption-clang-fgnuc-version), [GCC predefined macros](https://gcc.gnu.org/onlinedocs/cpp/Common-Predefined-Macros.html)

```cpp
#define JOIN_IMPL(a, b) a##b
#define JOIN(a, b) JOIN_IMPL(a, b)

int pick4(int)  { return 0; }
int pick14(int) { return 0; }

template<class T>
constexpr bool available = requires(T x) {
    JOIN(pick, __GNUC__)(x);
};

int main() {
    return available<int> ? 0 : 1;
}
```

There are no includes and no conditional directives.

- Clang analyses `pick4(x)`. Its AST supplies no dependency on `pick14`.
- The retained source and macro definitions contain `pick`, but no identifier token `pick14`.
- `pick14` has no other root or association obligation, so the specified rules permit removing it.
- GCC then sees a dependent requirement containing `pick14(x)`. For `T = int`, lookup fails and the requirement becomes false.

The original GCC executable returns **0**; the candidate returns **1**, and both compile. Substitution failure inside such a templated requirement produces `false`, rather than a compilation error. [C++ requires-expressions](https://eel.is/c++draft/expr.prim.req)

The static name invariant passes because `pick14` is absent from the retained, unexpanded source. The raw-versus-flat preprocessing check passes because resolution changed nothing. The pruned build passes.

The resolver’s rejection of `##` in **conditions** does not apply here.

**Fix:** Extend pruning eligibility to compiler-dependent expansions in ordinary user code. Every dependency-producing expansion must either be covered conservatively for the judge configuration or disable pruning. For an initial implementation, disabling pruning when an expansion can differ and its resulting names are not established is defensible. Merely resolving every `#if`, or possessing a snapshot that the Clang parse does not use, is insufficient.

**2. D3 blocker or explicit whitelist restriction needed, with either A2 decision: template selection can invalidate inertness.**

```cpp
int count = 0;

struct Effect {
    Effect() { ++count; }
};

template<int>
struct Select {
    using type = Effect;
};

template<>
struct Select<4> {
    using type = int;
};

using T = typename Select<__GNUC__>::type;

T unused;

int main() {
    return count;
}
```

Again, there are no conditional directives or system headers.

Clang resolves `T` through user-defined aliases to `int`. Under an AST-based interpretation of the stated “typedef chain” whitelist, `unused` is a scalar with absent initializer and may be deleted.

GCC 14 resolves `T` to `Effect`. Its initialization increments `count`. Removing `unused` changes the exit status from **1 to 0** while preserving successful compilation. Its name appears nowhere else, so the name invariant does not catch the deletion.

This example exposes a specification ambiguity: **if “typedef chain” already means a purely syntactic chain excluding template-selected aliases, say so explicitly.** Under that stronger interpretation, this particular example is retained.

**Fix:** Require a configuration-independent proof that every link in an accepted alias chain denotes the allowed scalar type. A conservative first version can exclude template-selected aliases and other semantically computed types from the whitelist unless their stability is established. The absence of user `#if` directives is not that proof.

This needs separate attention from hidden function names: preventing token-pasting removals alone does not establish inertness.

**3. Without the A2 extension, D3 additionally remains unsafe through standard-header macros—even with no user conditionals.**

Use an adversarial fixture for a listed standard header, such as `<cstddef>`, containing:

```cpp
#ifdef __clang__
#define __DISTILL_PROBE(x) 0
#else
#define __DISTILL_PROBE(x) hidden(x)
#endif
```

User code:

```cpp
#include <cstddef>

int hidden(int) { return 0; }

template<class T>
constexpr bool available = requires(T x) {
    __DISTILL_PROBE(x);
};

int main() {
    return available<int> ? 0 : 1;
}
```

This fixture is permitted by the **accepted** assumptions: its macro name is reserved, it changes no user macro or compiler-predefined macro, and A1 does not constrain its replacement list. It is not a claim about stock `<cstddef>`.

Clang sees the requirement `0;`, so `hidden` has no dependency from the retained AST or source tokens. Removing it makes GCC’s requirement false. The original returns **0**, the candidate **1**; both compile.

The only conditional is inside a system header, which §4.7 explicitly permits. Therefore the new pruning gate does not close this case.

The proposed **second clause** of A2 excludes this fixture. If that clause is declined, v5 needs a conservative rule for standard-header macro dependencies: retain all potentially referenced families or disable pruning when those dependencies cannot be established. Invalidating predefined macro states alone does not address this.

Also clarify whether “through their arguments” permits synthesizing names with `##`. Argument-derived names need not occur as identifier tokens in the invocation; GCC documents precisely that behavior. [GCC token concatenation](https://gcc.gnu.org/onlinedocs/cpp/Concatenation.html) Approval of that wording should not silently be interpreted as a stronger promise that every referenced identifier appears verbatim in an argument.

**The A2 decision has two distinct consequences.**

- **If approved:** predefined macros may survive standard-header transitions, and the hidden-name fixture above is excluded. Counterexamples 1 and 2 remain; neither uses headers.
- **If declined:** use the already specified invalidation of predefined macros. This reduces resolution opportunities and can consequently disable pruning. Separately, standard-header macro dependencies need the additional D3 fallback described above.
- Q5 overstates the consequence as profiles resolving “almost nothing.” Predefined-dependent conditions become unknown, but protected user macros and other independently established conditions can still resolve.

**Priority and verdict:**

1. **Before D2:** No remaining semantic blocker identified under the accepted assumptions and the specified conservative no-extension path. Do not enable the pending preservation assumption by default. A2 approval need not hold up D2.
2. **Before D3:** Replace the assertion that eliminating unknown conditionals establishes identical analysed user code. Specify conservative handling of ordinary compiler-dependent macro expansions, tighten the alias whitelist, and cover standard-header macro dependencies if A2’s second clause is not accepted.
3. **During implementation, with tests:** Exact macro-environment isolation, snapshot configuration scope, logical-location rendering/comparison, recursive-guard checks, family ownership and range pinning. Add the examples above with `--verify-run` absent and assert preservation or fallback.

My verdict would change once those D3 rules explicitly retain these dependencies or disable pruning, with regressions demonstrating the intended behavior. **A2 approval alone would not unblock D3, and successful compilation with the exact judge compiler would not detect these examples.**