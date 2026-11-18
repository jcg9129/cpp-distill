<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Review: constexpr and constinit variables as inert roots

Date: 2026-11-18. Direction approved by the user; implementation conditional on
this review finding the proposed rule sound.

**Verdict: the proposed rule is not sound. Phase 2 must not proceed.**
Constant initialization proves the absence of dynamic initialization of the
variable in the GCC-accepted program. It does not establish that Clang and GCC
agree on the variable's destruction. In particular, a non-dependent type that
Clang considers trivially destructible can have an effectful destructor under
GCC, even when both parses succeed with default constexpr limits. There is
also a separate lifetime-extended-temporary hole in checking only the declared
type. Keep the existing policy and whitelist pending a narrower proposal.

## Blocking counterexample: both compilers accept, destruction differs

This adapts the [constexpr-depth audit](2026-11-17-atl-pruning-blockers.md#step-2a-depth-audit-why-translation-remains-disabled)
to default compiler limits and a variable's type. No `-fconstexpr-depth` option
is needed, so the current disable for that option does not exclude this input.

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <type_traits>
constexpr int down(int n) { return n ? down(n - 1) : 0; }
constexpr int warmup = down(511);
template<int> struct tag {};
template<int N> concept deep = requires { typename tag<down(N)>; };
struct Quiet {};
struct Noisy {
  constexpr Noisy() = default;
  ~Noisy() { std::puts("destroyed"); }
};
template<int N> Quiet make(...);
template<int N> requires deep<N> Noisy make(int);
#ifndef REMOVE
constinit decltype(make<512>(0)) unused{};
#ifdef EXPECT_TRIVIAL
static_assert(std::is_trivially_destructible_v<decltype(unused)> == EXPECT_TRIVIAL);
#endif
#endif
int main() {}
```

Copy this block to `build/constexpr-inert-review/selection.cpp`, create
`build/constexpr-inert-review/tmp`, and run from the worktree root:

```sh
export TMPDIR="$PWD/build/constexpr-inert-review/tmp"
g++ -std=c++23 -O0 -pedantic-errors -DEXPECT_TRIVIAL=0 build/constexpr-inert-review/selection.cpp -o build/constexpr-inert-review/gcc-selection
build/constexpr-inert-review/gcc-selection
clang++ -std=c++23 -O0 -pedantic-errors -DEXPECT_TRIVIAL=1 build/constexpr-inert-review/selection.cpp -o build/constexpr-inert-review/clang-selection
build/constexpr-inert-review/clang-selection
g++ -std=c++23 -O0 -pedantic-errors -DREMOVE build/constexpr-inert-review/selection.cpp -o build/constexpr-inert-review/gcc-removed
build/constexpr-inert-review/gcc-removed
```

Observed with installed GCC 15.2.0 and Clang 22.1.8 on AArch64:

| Compiler | Type of `unused` | Original stdout | Stdout with `-DREMOVE` |
| --- | --- | --- | --- |
| GCC | `Noisy`, not trivially destructible | `destroyed` followed by newline | empty |
| Clang | `Quiet`, trivially destructible | empty | empty |

Both accept with `-pedantic-errors`. The table also holds for C++20 and C++23,
with `-O0` and `-O2`. The `EXPECT_TRIVIAL` assertions verify the type property
independently of output. All 16 original/removal compile-and-run checks passed.
Neither `make` overload needs a definition: the call is unevaluated.

The prior audit explains the cache/depth mechanism. GCC can reuse `down(511)`;
Clang's evaluation of the requirement exhausts its call budget and removes the
constrained candidate without a parse error. Here the differing selection
changes destruction, rather than just which function a call references.

Under the proposal, Clang's `unused` is `constinit`, non-dependent and trivially
destructible. It therefore passes even the seemingly safer trivial-destructor
branch, without relying on the controversial constexpr-destructor branch.
It has no incoming reference from `main`. Removing its declaration suppresses
GCC's exit-time output. Keeping the `Noisy` class or either overload alone
cannot recreate the removed object's destruction.

This is a language/compiler counterexample to the proposed inference, not a
measured pinned-worker deletion: LLVM 23.1.5 was not built or run. It remains a
proof obligation for that worker. Local successful parses, syntax-only keyword
checks, a non-dependent-type check, and the existing parse-error refusal do not
discharge it. The current whitelist already rejects the `decltype` type and
retains this variable as a root.

## Separate hole: the variable's destructor is not all the destruction

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <type_traits>
struct Reporter {
  constexpr Reporter() = default;
  ~Reporter() { std::puts("destroyed"); }
};
struct Trivial { const Reporter& ref; };
static_assert(std::is_trivially_destructible_v<Trivial>);
#ifndef REMOVE
constinit Trivial unused{Reporter{}};
#endif
int main() {}
```

GCC 15.2 accepts this under `-std=c++23 -pedantic-errors` and prints `destroyed`
at exit; deleting the variable makes stdout empty. Its initialization is static
and its declared type is trivially destructible. Its initializer nevertheless
creates a lifetime-extended temporary with a non-trivial destructor. A future
rule must account for destruction of all such temporaries, including reference
variables, aggregate reference members, and initializer-list backing arrays.
The lifetime rules are in [C++23 draft, class.temporary](https://timsong-cpp.github.io/cppwp/n4950/class.temporary).

Local Clang rejects this example, diagnosing unsupported non-trivial destruction
of a lifetime-extended temporary used in a constant-expression result. That is
an important limit on this example: the existing parse-error gate would prevent
pruning it locally. It is not evidence of an unsafe deletion through that gate.
It still disproves the claim that static initialization plus a trivial declared
type alone excludes exit effects in a GCC-accepted program. The first
counterexample does not depend on this Clang rejection.

## Attacks on the remaining cases

- **Constant initialization across compilers.** For an actually instantiated,
  GCC-accepted `constexpr` or initializing `constinit` declaration, the language
  requirement is stronger than Clang merely folding an initializer. GCC must
  honor that initialization requirement too. Thus unequal constant-evaluation
  budgets do not by themselves imply GCC will dynamically initialize that same
  variable. The unsafe step is importing Clang's selected type/destructor or
  dependencies as a proof about GCC. Clang can also accept a declaration GCC
  rejects; those programs are outside the assumed GCC-accepted input contract.
  See [basic.start.static](https://timsong-cpp.github.io/cppwp/n4950/basic.start.static)
  and [dcl.constinit](https://timsong-cpp.github.io/cppwp/n4950/dcl.constinit).

- **Clang errors and dependent ASTs.** `VisitDecl` disables on invalid declarations,
  `VisitStmt` on `RecoveryExpr`, and `extractPruningInWorker` on diagnostic errors.
  Keep these gates; `isConstexpr()` can survive error recovery and is not a
  success flag. A dependent initializer on a valid pattern is different: it may
  be unevaluated until instantiation. Checking the keyword avoids attempting
  to evaluate it prematurely but proves neither the final type nor all
  destruction properties. The default-depth example shows why a clean parse
  still is not an agreement certificate.

- **Non-trivial constexpr destructors.** C++20/23's requirement is constant
  destruction of the object, not merely a destructor declaration bearing
  `constexpr`. The test is a hypothetical constant evaluation; runtime semantics
  around `is_constant_evaluated()` need care. An initial probe with a constexpr
  object whose destructor prints only when `!std::is_constant_evaluated()`
  produced *no output under either local compiler*, including namespace,
  function-local static and function-local thread storage. It is not a working
  counterexample. Do not report it as one. [CWG 2962](https://cplusplus.github.io/CWG/issues/2962.html),
  listed as open in the consulted 2026-10-10 snapshot, addresses the missing
  manifest-constant-evaluation wording for destruction. See also
  [dcl.constexpr](https://timsong-cpp.github.io/cppwp/n4950/dcl.constexpr) and
  [expr.const, constant destruction](https://timsong-cpp.github.io/cppwp/n4950/expr.const#9).
  There is no need to resolve this issue to reject the proposal: the blocking
  example uses `constinit` and Clang's trivial-destructor branch.

- **`constinit` with a non-trivial destructor.** Retain as a root, including when
  the destructor is declared constexpr: the variable is not constexpr.
  Constant initialization does not suppress its exit effects under GCC. A
  local `constinit Reporter` probe with a constexpr destructor guarded by
  `!std::is_constant_evaluated()` printed under GCC but not Clang. Consequently
  using Clang's apparent constant destruction to broaden this branch would
  compound the problem. An unconditional printing destructor is the simpler
  retention control. [basic.start.term](https://timsong-cpp.github.io/cppwp/n4950/basic.start.term)
  covers destruction of statically initialized and thread-storage objects.

- **`thread_local constexpr`.** Thread storage also receives static
  initialization; `VarDecl::hasGlobalStorage()` includes it. A scalar or empty
  tag with a proven trivial destructor has no destructor registration effect
  to preserve. Effectful destruction, extended temporaries, volatile state,
  type-selection disagreement and attribute roots still require the same
  scrutiny. Do not drop the storage-duration check or assume thread exit is
  irrelevant. A future regression needs both unused and referenced cases.

- **Function-local `static constexpr`.** Its owner is the enclosing function.
  A genuinely inert local should not root an otherwise unused function; a
  call/reference still retains that function. If the function executes, any
  effectful local destruction or lifetime-extended temporary must remain.
  Constant initialization alone does not prove the absence of registration
  work for those cases. The local keyword-only probe above had no output;
  this observation is narrower than a general proof.

- **Class-template members and dependent types.** `static constexpr bool` on
  the `segment_tree` pattern enforces constant initialization for valid implicit
  instantiations that require the member definition, and `bool` has no
  destructor. A value-dependent initializer such as `!std::is_scalar_v<T>`
  need not cause a root merely because Clang cannot evaluate the pattern.
  But `static constexpr T` says much less about the selected destruction:
  requiring a non-dependent type would be a useful restriction, not a complete
  proof, as the blocking non-dependent `decltype` example demonstrates.
  Instantiations that are ill-formed do not need preserving as successful GCC
  executions. Explicit specializations need independent classification; a
  specialization does not inherit constexpr status from its primary.
  See [dcl.constexpr](https://timsong-cpp.github.io/cppwp/n4950/dcl.constexpr#1)
  and [temp.expl.spec](https://timsong-cpp.github.io/cppwp/n4950/temp.expl.spec).

- **Variable templates.** The same distinction applies to a constexpr primary
  pattern, instantiated definitions, partial specializations and explicit
  specializations. Do not propagate an inert decision from the primary to all
  declarations. Keep explicit-instantiation roots and existing ownership/unions.
  A future test must exercise an unused pattern, a referenced instantiation,
  and a specialization with an effectful destructor or initializer.

- **System addresses, ODR use and lookup without ADL.** Removing a *root reason*
  must not suppress traversal of its initializer, types, template arguments or
  references. `VisitDeclRefExpr` and `VisitMemberExpr` still add ordinary edges;
  `edge()` and `Dependencies::run()` traverse reachable system declarations and
  record system-to-user edges. Address-taking by retained user/system code
  must therefore retain the variable through that closure, without requiring
  ADL. Existing lexical-name and association rules remain conservative support.
  This uses the documented single-translation-unit/supported-system assumptions;
  it is not a claim to discover arbitrary external binary references. The
  counterexample has no reference to the object at all, so extra reference
  edges cannot fix it.

- **`[[gnu::used]]`, sections and other attribute roots.** `VisitDecl` roots
  interpreted `UsedAttr`, `SectionAttr`, constructor/destructor, weak,
  visibility, alias and asm-label cases. An inert result must only avoid
  adding a storage root, never clear a pre-existing attribute or other root.
  Unsupported attributes must continue to disable pruning. No change to
  attribute handling is justified by this proposal.

- **Volatile.** A spelling of constexpr/constinit is not a general exemption
  from volatile semantics. Preserve the current whitelist's rejection of
  volatile qualifications; any expansion to record types also needs to
  consider volatile subobjects. `constinit` does not add const qualification.
  There is no approved proof here for discarding volatile objects.

- **Mutable members.** Constexpr objects can have mutable members, and retained
  code can modify them. Such references must retain the object. Constant
  destruction's hypothetical lifetime assumption excludes mutable subobjects;
  do not assume the object and all its state are immutable. Both local compilers
  rejected a constexpr object whose destructor directly read a mutable boolean
  to decide whether to print, with constant-destruction diagnostics. That
  rejected probe is not a valid runtime counterexample. Mutable members alone
  neither justify removal nor universally require a root; destruction and
  reachability still need separate proofs. See
  [dcl.type.cv](https://timsong-cpp.github.io/cppwp/n4950/dcl.type.cv#4).

## Implementation boundary and follow-up

The current `VisitVarDecl` keeps the positive `inertType && safeSystemMacros &&
inertExpr` path and roots everything else with static/thread storage, except
non-defining static-member declarations. It rejects the blocking variable's
syntactic `decltype` type. Class records remain whole families. The related
policy is documented in `docs/design.md`, keep rule 1.

A narrower proposal could target fixed, syntactically known scalar types on
constexpr template patterns and empty, non-dependent tag types whose complete
destruction is invariant. It would need an explicit argument excluding hidden
lifetime-extended destruction, compiler-dependent type selection, volatile
objects and bypass of existing attribute/system/dependency gates. Merely
replacing the proposal with a Clang trivial-destructor test is insufficient.
This review does not approve or implement that different rule.

Per the two-phase gate, there is no change to `src/clang/prune_extract.cpp`,
`pruningPolicyVersion`, `docs/design.md`, extraction tests or runtime fixtures.
Implementing phase 2 as written would admit the counterexample above.

## Verification

All generated sources, binaries, compiler temporary files and build outputs
were placed under this worktree's ignored `build/` directory. No LLVM build,
commits or credential searches were performed.

- Installed GCC 15.2.0 and Clang 22.1.8, AArch64: 16 compile-and-run checks for
  the blocking source, covering both languages (C++20/C++23), optimization
  levels (`-O0`/`-O2`), and original/removal variants. Type assertions and exact
  output assertions passed. These are not LLVM 23.1.5 worker tests.
- Additional compiler probes established the lifetime-extended-temporary
  behavior and Clang refusal, the unproductive constexpr-destructor attack,
  the mutable-member rejection, and the constinit destruction discrepancy
  described above.
- Configured an LLVM-free Debug build with `DISTILL_LLVM_DIR` empty and
  `DISTILL_PROFILE_ONLY=OFF`; `cmake --build build -j2` passed with
  `CMAKE_CXX_FLAGS_DEBUG='-O0 -g0'`. The initial build with debug symbols was
  killed in `cc1plus`; disabling debug symbols allowed the build to complete.
- `ctest --test-dir build --output-on-failure --timeout 120 -j2`, with
  `TMPDIR` inside the worktree: all six LLVM-free suites passed (unit,
  inline cases, resolver cases, pipeline cases, checker models, and caide
  assertion tokens). Worker extraction tests require the SDK worker and were
  not run.
- Whitespace check of the new review passed; git status contains only this
  new review document outside ignored build outputs.
