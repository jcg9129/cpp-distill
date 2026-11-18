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

## Narrow rule (2026-11-18)

**Verdict: the narrow rule is also unsound. Phase 2 must not proceed.**
The restrictions exclude the previous destruction counterexample, but they do
not exclude an initializer's effect on GCC's constexpr evaluation cache.
Deleting an otherwise unused arithmetic `constexpr` variable can change a later
constraint's satisfaction and therefore the GCC executable's output. Both the
original and the deletion are accepted by both installed compilers at default
constexpr limits. This also exposes a pre-existing hole outside variable
pruning, detailed below; it is documented here without a fix.

The earlier sections were restored from the report copy supplied with the task,
which was absent in this worktree. Their verification describes that earlier
review. The checks at the end of this section were performed in this worktree.

### Blocking scalar counterexample: removing cache warmup changes selection

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
constexpr int down(int n) { return n ? down(n - 1) : 0; }
#ifndef REMOVE
constexpr int warmup = down(511);
#endif
template<int> struct tag {};
template<int N> concept deep = requires { typename tag<down(N)>; };
template<int N> int choose(...) { return 1; }
template<int N> requires deep<N> int choose(int) { return 2; }
int main() { std::printf("%d\n", choose<512>(0)); }
```

`warmup` meets A, B1 and C exactly: namespace storage, `constexpr`, builtin `int`
as written, no volatile/reference/thread-local/attribute/alias/deduction, and a
valid constant initializer. Neither its type nor destruction differs between
compilers. No expression refers to `warmup`; its outgoing edge to `down` does
not create an incoming dependency retaining it. The existing whitelist roots
it because `inertExpr` rejects the call; the proposed rule would remove that
root and allow its deletion.

Save the block as `build/constexpr-inert-review/narrow-warmup.cpp` and run:

```sh
export TMPDIR="$PWD/build/constexpr-inert-review/tmp"
g++ -std=c++20 -O0 -pedantic-errors build/constexpr-inert-review/narrow-warmup.cpp -o build/constexpr-inert-review/original
build/constexpr-inert-review/original
g++ -std=c++20 -O0 -pedantic-errors -DREMOVE build/constexpr-inert-review/narrow-warmup.cpp -o build/constexpr-inert-review/removed
build/constexpr-inert-review/removed
```

| Compiler | With `warmup` | After removing `warmup` |
| --- | --- | --- |
| GCC 15.2.0 | `2` followed by newline | `1` followed by newline |
| Clang 22.1.8 | `1` followed by newline | `1` followed by newline |

The result holds for C++20/C++23 and `-O0`/`-O2`, always with
`-pedantic-errors`, no constexpr-limit flags, and successful exit status. GCC
can reuse the already evaluated `down(511)` when checking `down(512)` in the
requirement. Without that cached evaluation, the requirement fails at the
default recursion limit and the constrained overload is removed. This is an
observable compiler resource/cache effect, not a side effect in the C++
abstract machine or a dynamically initialized variable. The supported-input
contract includes these GCC-accepted programs; constant initialization of
`warmup` does not promise preservation of subsequent overload choices when its
source is deleted. Raising or matching Clang limits is not a remedy, as the
previous depth audit explains.

The same attack applies directly to the proposed class-template-member
exception. Replace the guarded variable with:

```cpp
template<class T> struct Unused {
  static constexpr bool warmup = down(511) == 0;
};
```

No instantiation of `Unused` is needed. Local GCC evaluates the non-dependent
initializer while processing the template. Deleting the unused record changes
stdout from `2\n` to `1\n`; Clang prints `1\n` in both cases. This member has
the required as-written builtin `bool` type, so the narrow rule would cease
rooting its enclosing record.

B2's particular definition restrictions also allow nested enumerations. Replace
the guarded variable with both of these declarations:

```cpp
struct commutative_tags_t { enum { warmup = down(511) }; };
inline constexpr commutative_tags_t commutative_tags{};
```

This named, non-template, non-local user class has no bases, non-static data
members, user-declared special members, virtual functions or attributes, and
is not a specialization. Its empty initializer and trivial destruction meet
B2/C. With its variable no longer a root, the unused record and variable can
both disappear, again changing GCC's output from `2\n` to `1\n`. The nested
enumerator is not a non-static data member. All four variants were checked
across the compiler/language/optimization matrix above. The scalar example
alone is sufficient to reject the proposal, even if B2 were tightened further.

### Requested attacks and intended beneficiaries

- **Lifetime extension:** an arithmetic result has no reference through which
  to extend a temporary's lifetime. An empty tag with no bases or non-static
  data members cannot contain reference members or an initializer-list backing
  array, and A excludes reference variables. These restrictions exclude the
  earlier `Trivial{Reporter{}}` attack. Checking only Clang's canonical scalar
  or trivial-destructor properties would not establish those restrictions.
- **Implicit conversions and temporary destruction:** B1 permits calls and
  conversions creating class temporaries. In a GCC-accepted `constexpr`
  arithmetic initializer their evaluated construction, conversion and
  full-expression destruction must be valid constant evaluation; the scalar
  does not extend their lifetime. A local `constexpr int converted = Convert{};`
  probe, with a constexpr conversion and a constexpr destructor printing only
  outside constant evaluation, produces no output before or after deletion
  under either compiler. That is not a runtime-destruction counterexample.
  It does not rule out compile-time cache effects, which the blocking call
  demonstrates. For B2, genuinely empty initialization and the absence of
  user-declared special members exclude user conversion/constructor arguments;
  merely seeing a zero-argument constructor after applying default arguments
  would not prove the initializer's required source form.
- **`decltype`/overload divergence:** B1's as-written restriction prevents
  Clang's selected canonical `int` from concealing `auto`, `decltype`, an alias
  or a template parameter; B2 must likewise inspect source type spelling,
  including qualifiers, and reject template-dependent type selection rather
  than peeling it to a record. That excludes the earlier `decltype(make(...))`
  destruction attack. It does not exclude the new counterexample: its source
  type is literally `int`, and the later overload selection changes instead.
- **Macro-produced types:** user-only macros expanding to builtin keywords or
  a direct eligible tag name need the same expanded TypeLoc checks; an alias
  or `decltype` expansion must not be accepted via its canonical type. The
  predefined-macro equality checks, external-paste refusal, unsupported-attribute
  gates, macro ownership/pinning and `safeSystemMacros` restriction must still
  apply. A macro spelling of `constexpr` alone is not evidence about its type
  expansion. The counterexamples need no type-producing macro, system numeric
  macro or compiler-conditional source; `REMOVE` only controls the test deletion.
- **`commutative_tags` and `segment_tree`:** the proposal would remove the
  storage root from `struct commutative_tags_t {}; inline constexpr
  commutative_tags_t commutative_tags{};` and from an unused class template's
  `static constexpr bool x = !std::is_scalar_v<T>;`, despite that initializer's
  dependence. Both forms compile in the local positive probe. References must
  still retain the tag variable/type or the whole template record through
  ordinary edges and source names. Explicit specializations require their own
  classification. Those intended cases fit the proposal, but the counterexamples
  show why that does not establish safety of the proposed general rule. No
  pinned-worker removal or retention results are claimed for these examples.
- **Other exclusions and gates:** `constinit`, thread storage, volatile,
  references, nonempty records, user-declared destructors and parameter-typed
  members supply no new exemption under A/B. Supported attribute handling must
  not clear a previously assigned root; unsupported attributes and parse/error
  recovery still disable pruning. Keeping traversal of initializers, type
  spellings and template arguments remains necessary even after an inert result.

### Existing retention: alternate overload dependencies versus cache effects

The report's cross-compiler overload choice does **not by itself** show that
only the Clang-selected overload's differently named helper is retained. The
current implementation supplies several conservative paths:

1. `Consumer::HandleTranslationUnit` and `Dependencies::VisitFunctionDecl` union
   non-member overloads by primary declaration context and unqualified name.
   Thus keeping `choose` keeps both overload definitions, even if Clang only
   instantiates one. Overloads in different namespaces still share their
   indexed unqualified spelling for the global lexical name rule.
2. `Extraction::owner` follows implicit instantiations back to primary templates
   and patterns, while explicit specializations have direct owners and are
   joined to their user template families. `Dependencies` traverses patterns
   and known instantiations; it does not replace the pattern with Clang's chosen
   instantiation. In `computeRetention`, identifiers in **each kept family's
   source text** retain all matching indexed names. If the two overload bodies
   call `helper_a` and `helper_b`, both helpers stay even though they have
   different names. Neither needs a Clang call edge from `main`.
3. Unqualified dependent call names in patterns are recorded by
   `VisitUnresolvedLookupExpr`; retention keeps matching callable families
   across namespaces. Whole-record ownership, type carriers and association
   retain member alternatives, including out-of-line definitions. The ADL
   registry separately roots known standard customization names without
   requiring the local compiler to choose a particular hook. Reachable system
   declarations are traversed for system-to-user dependencies. The registry
   is a bounded standard-header assumption, not a list of all arbitrary helper
   names or a proof that compiler constant-evaluation caches are identical.

Nevertheless **there is a pre-existing soundness hole**: deletion of a
compile-time cache seed outside variables. Here is a complete reproducer with
both differently named helpers:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
constexpr int down(int n) { return n ? down(n - 1) : 0; }
#ifndef REMOVE
enum Seed { warmup = down(511) };
#endif
template<int> struct tag {};
template<int N> concept deep = requires { typename tag<down(N)>; };
int helper_a() { return 1; }
int helper_b() { return 2; }
template<int N> int choose(...) { return helper_a(); }
template<int N> requires deep<N> int choose(int) { return helper_b(); }
int main() { std::printf("%d\n", choose<512>(0)); }
```

`Extraction::discover` creates an enumeration family with indexed names `Seed`
and `warmup`, without a root. Neither name appears outside that family. Its
outgoing AST/source dependency on `down` does not retain it backwards, and an
enum's initializer is not subject to `VisitVarDecl`'s storage-root rule. It has
no function/specialization association retaining it. Consequently the present
rules allow deleting the enum while retaining `down`, `tag`, `deep`, both
`choose` patterns, `helper_a`, `helper_b` and `main`. Both helper definitions
survive; GCC changes which helper runs because deleting the enum changes
constraint evaluation. Original GCC stdout is `2\n`, deletion stdout `1\n`;
Clang prints `1\n` for both. Both compilers accept both forms at default limits.

An LLVM-free hand-built model linked against the actual `distill_core` verified
this retention trace: the enum alone was removed, both overload families and
helpers were retained, and `staticNameInvariant` passed on the deletion. The
model includes a main-to-fallback AST edge, the overload union and the source
name index; the alternate helper remains retained without a selected call
edge. Compiling its original and pruned sources with GCC reproduced `2\n`
versus `1\n`. This checks the current core policy and the compiler mechanism,
not extraction by the unavailable pinned worker. The extraction conclusion is
from the source trace above. Preserving all alternate definitions cannot
restore the cache seed, so the missing guarantee is larger than an overload
family or ADL edge. This PR does not fix that existing hole.

### Verification of the narrow-rule review

All generated probe sources, scripts, results, binaries, temporary files and
build outputs are under this worktree's ignored `build/` directory; `TMPDIR`
was set to `build/constexpr-inert-review/tmp` for compilation and testing.

- Installed GCC 15.2.0 and Clang 22.1.8 on AArch64: **80 asserted compile-and-run
  checks** for scalar warmup, class-template bool warmup, empty-tag nested-enum
  warmup, existing enum warmup and initializer-conversion controls. Each covers
  C++20/C++23, `-O0`/`-O2` and original/removal variants, with `-pedantic-errors`
  and default constexpr limits. Exit status, exact stdout and empty stderr
  were checked. Results are in `build/constexpr-inert-review/results.json`;
  `run-probes.py` in that directory reproduces the matrix.
- The linked LLVM-free retention model and the original/pruned GCC executions
  check the existing enum hole independently of the proposed variable change.
- LLVM-free Debug configuration: `DISTILL_LLVM_DIR` empty,
  `DISTILL_PROFILE_ONLY=OFF`, `CMAKE_CXX_FLAGS_DEBUG='-O0 -g0'`;
  `cmake --build build -j2` passed. Existing missing-field-initializer warnings
  in `src/core/profile/profile.cpp` were unrelated to this documentation change.
- `ctest --test-dir build --output-on-failure --timeout 120 -j2` passed all
  **six LLVM-free suites**: unit, inline cases, resolver cases, pipeline cases,
  checker models and caide assertion tokens (147.63 seconds total).
- Whitespace checks passed. The only non-ignored change is this review document;
  `src/clang/prune_extract.cpp`, policy `D3d-2`, `docs/design.md`, extraction tests
  and runtime fixtures remain unchanged because the phase-1 soundness gate
  failed. No LLVM build, pinned-worker tests, commits, credential searches or
  writes outside the worktree were performed. The pinned LLVM 23.1.5 extraction
  behavior remains unmeasured; local compiler probes and the core model must not
  be represented as worker end-to-end tests.

## Under A7 (2026-11-18)

**Verdict: the narrow rule is sound under A1–A7 and the existing pruning
preconditions. Phase 2 may proceed.** This supersedes the narrow-rule verdict
above for this amended contract; it does not approve the original broad
constexpr/constinit proposal.

The user added **A7** (2026-11-18): “The program's observable behaviour does not
depend on constant evaluation reaching an implementation limit (constexpr call
depth, loop/operation limits, template instantiation depth); in particular no
constraint, SFINAE or if-constexpr outcome is decided by limit exhaustion or by
compiler evaluation caches.” Every pruned artifact relies on A7, including
artifacts produced without using the new variable exception. The report must
list A7 alongside its other applicable assumptions.

### Exact boundary approved here

Keep the existing positive `inertType && safeSystemMacros && inertExpr`
whitelist. Add an alternative storage-root exemption with all of:

- **A:** a defining `constexpr` variable with static storage, no thread storage,
  no volatile or reference type and no attributes on the variable. `constinit`
  alone supplies no exemption. Existing attribute, macro, parse-error,
  dependency, ownership and explicit-instantiation rules still apply.
- **B1:** its expanded, as-written type is builtin arithmetic (`bool`, character,
  integer or floating types), ignoring cv qualification only. No canonical-type
  inference, aliases, `decltype`, `auto`, template-parameter/member types,
  pointers, arrays or enums gain this exemption. Its initializer may be
  value-dependent on a template pattern; the type must remain fixed as written.
- **B2, alternatively:** the written type directly names a complete, named,
  non-local, non-template, non-dependent user class, not a union or class
  specialization. It has no bases, non-static data members, user-declared
  constructors, copy/move assignments or destructor, virtual functions, or
  attributes. Its destructor is trivial. Require an actually empty list
  initializer (`{}` or `= {}`), not an arbitrary expression that Clang happens
  to lower to a zero-argument construction. Nested enums and static members
  remain subject to ordinary traversal and their own root classification.
- **C:** `safeSystemMacros` still holds. The exemption only avoids adding a
  storage root; it never erases an existing root or skips dependency traversal.

A class nested inside a template/specialization does not get B2 through an
instantiated concrete type. Explicit variable/member specializations are
classified from their own written declarations, never from the primary's
constexpr status or type. Existing family unions mean an effectful
specialization retains its primary as well.

### Remaining attacks

The scalar warmup, template-member warmup and empty-tag nested-enum warmup
counterexamples above all change a constraint through limit exhaustion and
compiler caching. They violate A7. **A7 also closes the pre-existing enum
cache-seed hole in the contract**, without requiring an enum-root code change:
removing a cache seed cannot observably change an A7-conforming program's
selection. This is a restriction of supported programs, not a claim to repair
compiler caches or detect every A7 violation.

**Lifetime extension:** excluding references alone was insufficient for the
original proposal, because a class could contain a reference member. B1 has
no such member; B2 excludes all non-static data members and bases. Neither can
carry a reference or initializer-list backing array with a lifetime-extended,
effectful temporary. See [class.temporary](https://timsong-cpp.github.io/cppwp/n4950/class.temporary).

**Implicit conversions:** B1 can create class temporaries while computing an
arithmetic result. A valid instantiated constexpr declaration requires the
entire initialization full-expression, including destruction of those
temporaries, to be constant evaluation. There is no runtime full-expression
left with an effectful temporary destructor, nor a scalar reference extending
its lifetime to program exit. A dependent initializer need not be evaluated
on the uninstantiated pattern; valid GCC instantiations still have the same
requirement. Ill-formed instantiations are outside successful-input semantics.
B2's source-empty aggregate initialization and structural restrictions exclude
user constructors, conversion arguments and default arguments. See
[dcl.constexpr](https://timsong-cpp.github.io/cppwp/n4950/dcl.constexpr).

**Types and macros:** inspecting expanded TypeLocs, without desugaring aliases
or deduced types, rejects `decltype`, `auto` and alias spellings even if Clang's
canonical result is arithmetic or an empty record. User macros producing
builtin keywords or direct eligible tag names can qualify; macros producing
aliases or `decltype` cannot. External paste, predefined-macro equality,
system-macro restrictions, macro ownership/pinning and unsupported-attribute
refusal remain independent gates. A7 does not excuse any preprocessing or
cross-compiler type-selection disagreement unrelated to resource exhaustion.

**Specializations and destruction:** explicit specializations can have a
different type and different constexpr status; inspect each separately and
retain a specialization with an effectful initializer/destructor. For B2, an
ordinary non-template class with no bases, fields or user-declared special
members has no hidden destruction to import from Clang's choice of an overload
or specialization. Nonempty classes, references, `constinit`, thread storage
and template-parameter-typed members receive no new exemption. None of the
remaining requested attacks survives these restrictions under A7. See
[temp.expl.spec](https://timsong-cpp.github.io/cppwp/n4950/temp.expl.spec).

### Reconsidering parse flags without implementing translations

**Yes: a resource-only `-fconstexpr-depth=N` translation is sound under A7**
for GCC-accepted supported inputs, subject to the existing error/refusal gates.
The earlier equal-limit/cache and larger-Clang-limit constraint counterexamples
all violate A7. A larger Clang budget can turn a GCC depth-failed requirement
into a successful requirement only when that outcome depended on exhaustion,
which A7 expressly excludes. A GCC rejection of a required constant expression
is outside the accepted-input contract. A lower/equal Clang budget that emits
a diagnostic must still disable pruning; silently changed constraint outcomes
are excluded by A7. This establishes the resource-policy argument, not general
GCC/Clang semantic equivalence. The pinned option spelling was audited in the
[earlier depth audit](2026-11-17-atl-pruning-blockers.md#step-2a-depth-audit-why-translation-remains-disabled).
A7 similarly removes the resource/cache objection to loop/operation-budget
handling, but does not establish an exact GCC-loop/GCC-operation to Clang-step
mapping. Numeric validation, ordered options, the pinned cc1 mapping and worker
regressions still belong to a separate implementation. **No flag translation
or omission is implemented here.**

A7 supplies no argument for OpenMP, modules, CPU options, external linkage or
trivial-auto-init semantics. Their separate audit obligations remain. Nor does
A7 justify the original broad destructor inference or bypass the error-recovery
gates. The keep-rule amendment below is deliberately limited to A/B/C.

### Phase 2 implementation and verification under A7

Policy **D3d-3** implements the additional exemption in `VisitVarDecl` using
expanded TypeLocs and the empty record's definition and syntactic initializer.
Implicit instantiations consult the pattern's written type; explicit
specializations keep independent classification. The old positive whitelist,
root reasons, traversal, unions and refusal gates remain in place. Every
enabled retention result carries A7, and the pipeline propagates it only after
successfully checking a pruned artifact. Disabled analysis and flat/raw
fallback artifacts do not acquire A7 as an output assumption.

Extraction regressions cover unused/referenced constexpr-bool class templates,
empty tags, arithmetic calls, conversion temporaries, local statics, aliases,
`decltype`, `auto`, `constinit`, thread storage, volatile, references, nonempty
classes, destructors (including defaulted), constructors/default arguments,
bases, class specializations, attributes, macro-produced types,
template-parameter-typed members and different-type explicit specializations.
The `constexpr_inert` pipeline fixture checks whole-template/tag removal and
retains an unused specialized object whose destructor prints. Report tests
cover deletion, no family deletion, disabled analysis and rejected candidates.

Persistent probe sources, downloaded API headers, binaries and build outputs
are under this worktree's ignored `build/` directory. All test temporary
directories remain inside the worktree; compiler `TMPDIR` is `build/tmp`.
No LLVM build, commits or credential searches were performed.

- GCC 15.2.0 and Clang 22.1.8 on AArch64: **16 asserted compile-and-run checks**
  of the runtime fixture and a manually reduced control across C++20/C++23 and
  `-O0`/`-O2`, with `-pedantic-errors`. Every execution exits 0 and prints
  `0 1\nspecialized\n`; the conversion temporary produces no runtime output.
  These controls exercise the language argument, not worker extraction.
- All **28 new header-free extraction inputs** pass syntax checks with both
  installed compilers in C++20, including every negative retention input.
  Sources and `a7-results.json` are in `build/constexpr-inert-review/`.
- The LLVM-free Debug build uses `DISTILL_LLVM_DIR` empty,
  `DISTILL_PROFILE_ONLY=OFF`, `CMAKE_CXX_FLAGS_DEBUG='-O0 -g0'` and
  `cmake --build build -j2`. The extraction-test harness builds without LLVM;
  `src/clang/prune_extract.cpp` itself requires the pinned SDK and was not
  compiled in this build. Existing profile missing-initializer warnings remain.
- The new pinned-worker extraction regressions and worker-backed pipeline
  fixture were **not run**. No pinned LLVM 23.1.5 extraction/removal result is
  claimed from the installed compiler probes or LLVM-free tests.
- LLVM-free CTest run (`--output-on-failure --timeout 120 -j2`): inline cases,
  resolver cases, pipeline cases, checker models and caide assertion tokens
  passed. The unit suite reached its existing 120-second timeout with no
  failed assertions, after passing the pruning regressions and first A7
  pipeline report test. Rerunning the complete unit executable alone passed:
  **247 passed, 0 failed, 1 platform skip; 0 failed checks**, including both
  A7 pipeline report cases and fallback exclusions. See `build/unit-rerun.log`.
- `git diff --check` passed. Changes are uncommitted; parse-option translation
  remains unchanged.
