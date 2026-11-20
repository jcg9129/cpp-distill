# Implicit instantiation effects — 2026-11-20

Policy D3d-5 repairs a missing direction in the retention relation. A template
being retained is insufficient: deleting an uncalled function, a class member
body, or even a declaration can delete an implicit instantiation. This is a
semantic effect, not the implementation-limit/cache sensitivity excluded by A7.

## Effects and non-effects

The supported C++ constructs with an effect that can escape their immediate
instantiation are conservatively classified as follows:

* **Function friends in template contexts.** Instantiating a class can supply a
  definition for a function declared elsewhere, including a declaration in an
  unrelated argument type. Return-type deduction can then change a later
  requires-expression. This applies to primary, partial, member and nested
  class templates, and classes nested in other template contexts. No reliance
  is placed on the partial specialization selected by the worker. All function
  friend declarations, including function templates and declarations without
  definitions, are classified; the implementation does not attempt to prove
  signature disjointness across compilers.
* **Static/thread storage outside the existing inert exemptions.** Instantiating
  a variable template or using a class-template static member can introduce
  dynamic initialization or destruction. Rooting the pattern, as rule 1 already
  did, does not preserve that use. Function-template static locals, member
  function static locals, generic-lambda statics and out-of-line definitions
  receive the same conservative treatment. A local static's initialization
  normally runs only when control reaches it; treating all its possible
  instantiation triggers as significant is safe over-retention.
* **Interpreted attribute roots in template contexts.** Constructor/destructor
  attributes and forced-emission or symbol attributes can give an instantiated
  definition significance independent of a normal call. Every existing
  interpreted attribute root is conservatively an effect source when templated.
  Interpreted assembly or alias symbols referencing a known template entity also
  seed the rule: a retained pattern alone need not emit the specialization that
  an opaque linker-symbol reference needs. Symbol resolution records which
  families contain template entities before processing those references.
  Unknown attributes, assembly and other unsupported forms retain their refusal
  gates; this change does not expand the accepted extension surface.

Class-valued non-type template parameters introduce static template parameter
objects, not `VarDecl`s ([C++20 temp.param/8](https://timsong-cpp.github.io/cppwp/n4861/temp.param#8)).
Constant destruction is required, but the policy does not infer that this makes
all emitted storage/destruction irrelevant; constant-expression eligibility is
distinct from ordinary termination's storage-duration rules
([basic.start.term](https://timsong-cpp.github.io/cppwp/n4861/basic.start.term)).
The NTTP proof is separate from the static-variable whitelist. Direct scalar,
enum, pointer, member-pointer and reference parameters cannot introduce parameter
objects. In particular, a pointer/reference to a class does not create an object
of that class. User aliases are followed without discarding their written
system-template selections; C/C++ standard integer and size typedefs are known
scalar by contract. `UsingType` must be handled as well as `TypedefType`: Clang
represents `std::uint32_t` imported from `::uint32_t` by a `UsingType` under the
user alias `atl::uint32`. The old whitelist rejected that node. This was a type
inspection bug, not a class-valued `mod_int32` parameter.

A non-template user class with a trivial destructor is also harmless when every
base and field recursively has provably harmless destruction. Template arguments
are converted constant expressions ([temp.arg.nontype]/2,
https://timsong-cpp.github.io/cppwp/n4861/temp.arg.nontype#2); observable
address/value uses are already
ordinary dependencies. With trivial destruction there is no independent runtime
initialization or termination code for reverse retention to preserve. Checking only
Clang's `hasTrivialDestructor()` is insufficient: a field selected through a
system template can have different destruction on the judge. System template
selections remain conservative even when their local canonical type is scalar,
including selections hidden under user aliases or in members. Type-qualified
names also stay conservative: `Pick<sizeof(long)>::type` can select a scalar
typedef or enum locally and a class in another user specialization on the judge.
Clang 23 stores that qualifier on the named type node, so it must be checked
before following a typedef or exempting an enum. Classes with
nontrivial destruction remain sources. Constant destruction is **not** used as
a blanket exemption: both compilers accept a constexpr destructor that calls
`puts` outside constant evaluation, although neither probe executes that
parameter object's destructor at termination. The standard's constant-evaluation
requirement alone does not prove that runtime destruction is unobservable.

**Dependent/placeholder exception remains conservative.** Neither a missing
specialization nor only known scalar bindings proves absence of a class binding
on another judge. `unseen_argument.cpp` in the probe directory below has
`Object<1>` locally, plus an uninstantiated `Object<T{}>` inside a function
pattern. A user `if constexpr` branch selects that function with `T=Value` on
another target. The probe's `CLASS_JUDGE` switch models a user expression such
as `sizeof(long)==4`; this does not require any header to introduce a user name,
invoke a hook or change a feature macro. Clang's AST has no class-valued argument
with the switch off and does with it on. Defining `NO_KNOWN` removes the known
scalar bindings and gives the same counterexample with no known specialization
at all. Both `auto` and a dependent `T` parameter are probed. A1 restricts header
names, A6 restricts library hook names, and A9 restricts library feature macros;
none forbids this user type/value selection or fixes target sizes. Library
traits can also select different user branches. Thus the requested known-bindings
exemption cannot be justified under the existing assumptions, and `auto`,
constrained `auto`, `decltype(auto)` and dependent member types remain sources
unless their written shape itself excludes an object (e.g. `T*` or `auto&`).
Unknown ownership of a materialized parameter object still triggers the ordinary
unowned-dependency refusal.

A default member initializer is not itself a new global runtime effect: its
runtime side effects require construction. Ordinary retained uses preserve that
construction and its dependencies. However, compiling a default initializer,
constructor, default argument, constraint or unevaluated operand can instantiate
one of the effect sources above. Those paths must be preserved even when the
surrounding function is never executed. The consteval/default-member-initializer
case exercises this distinction. Calls used during constexpr/consteval evaluation
are likewise dependency paths, including initializers exempted by D3d-3. No
constant evaluator is used to decide whether a trigger is harmless.

Ordinary non-static members, bases, virtual members, layouts and implicit special
members have no independently running initialization merely because a class is
instantiated. Their effects require an object, call or retained type operation,
whose ordinary dependency closure already preserves them. An inert scalar static
or whitelisted constexpr definition has no separate dynamic initialization or
nontrivial destruction; its initializer can still lead to another effect source
and is traversed normally. Friend class/type declarations alone do not define
functions or introduce executing storage. Explicit instantiations and namespace
static assertions already are roots. Ill-formed instantiations are covered by the
original-build and parse-error gates. Resource-limit and compiler-evaluation-cache
observations remain excluded only to the extent stated by A7; friend injection
and dynamic initialization are not excluded by A7.

## Potential triggers and the rule

The extractor records `instantiationEffect` on the owning family, inspecting
patterns as well as known specializations. Template-context detection walks
semantic enclosing declarations, so nested classes and out-of-line members are
included. Existing primary/partial/specialization and lexical ownership unions
remain authoritative. The worker protocol advances to DISTILL-PRUNE-2 for this
additional field; old workers fail closed.

Core builds a directed potential dependency graph independent of retention:

1. Family to every family whose indexed name occurs as an identifier token in
   its text. Comments and string contents do not participate.
2. Family to its AST references and dependent-call name matches.
3. Type carrier to its associated function/specialization families, as in rule 3.
4. Both directions for a family union.
5. Family/system entity to its positive system/user dependency edges.

Starting at effect sources, follow this graph **backward**, retaining all reached
user families. Retain the sources themselves, then run the existing forward
fixpoint normally. A token that merely names a template is enough; there is no
attempt to determine whether it requires completeness, odr-uses storage, executes,
or selects a particular specialization. Transitive closure is essential: an alias
may only name a specialization, while a caller of the alias requires completeness;
a default argument can hide a further carrier. Declaration-only ranges, dormant
bodies, uninstantiated patterns and all their tokens participate equally. The
existing macro provenance and dependent-name rules cover expanded identifiers;
macro-produced declarations remain pinned. The graph is skipped when there are
no effect sources and capped at four million edges otherwise. A cap violation
retains everything with an explicit disabling reason.

A nontrivial incoming path from a library can be absent on the worker's target.
A6 only bounds the names of hooks; it does not say that instantiating a removed
library use cannot instantiate a rooted user hook's body. Consequently, if reverse
closure reaches a free ADL/operator hook or a user specialization of a system
template, the whole pruning analysis refuses with
`unbounded system instantiation effects through <family>`. A record-owned hidden
friend has a bounded carrier path only with the extractor's `boundedSystemHooks`
certificate: every non-member callable in that family must have a parameter
spelled directly as a record in the same family, optionally through pointers or
references. Class-template names and injected class names qualify; aliases and
substituted parameter types do not. Certificates from patterns and instances are
intersected. Merely having a nonempty associated-types list is insufficient:
those types can come from unrelated template arguments or another overload.
A record-owned hook without this certificate also refuses. Member customization
and callbacks have their ordinary type/value carriers. Positive system edges are
included without using their absence as evidence of safety.

The common hidden-friend `operator==(S, S)` is deliberately conservative: its
family was already an operator root, and now its possible instantiation triggers
stay too. Pruning remains enabled when its carrier is bounded. A blanket exemption
based only on that parameter spelling would require checking other declarations:
a namespace redeclaration and address-taking can require the injected definition
without itself requiring a complete S. Return-type deduction and other friend
signatures offer further pitfalls. This policy does not claim a signature-based
proof. The hidden-friend fixture checks that unrelated records remain removable.

## Soundness under A1–A7, A9

The proof extends, rather than replaces, the existing §4.7 dependency argument.
For every removed family, there is no potential dependency path from it to an
effect source. Otherwise the reverse closure would retain it. Thus deletion cannot
remove the last trigger of an effectful instantiation along a supported user
lookup/type/value path. Patterns and textual names include alternatives not
selected by the local target; associations preserve implicit dispatch through
user type carriers, and the unbounded library entry cases refuse. Once triggers
survive, the existing forward closure preserves their complete definitions,
lookup candidates and transitive dependencies. Whole-record retention preserves
members, friends and out-of-line definitions together. No claim is made that a
missing Clang instantiation means a missing judge instantiation.

A1 bounds header-introduced names; A2 and the macro/paste gates preserve user
identifier carriers. A3–A5 and, where used, A9 justify the resolved preprocessing
environment. A6 supplies the library hook surface used for the refusal check,
with operators handled separately. A7 permits removal of otherwise inert
instantiation/cache seeds but does not justify removal of either reported effect.
No new trust assumption is added, and no existing gate is relaxed. Constructs
whose ownership, AST traversal, attributes or incoming library triggers cannot
be bounded disable pruning rather than relying on the worker's selected branch.

## Verification

New prune cases cover the friend-injection body and declaration-only forms,
static-member registration, the self-associated hidden-friend idiom, nested member
partial specialization through an alias/default, variable-template registration,
consteval evaluation from a default member initializer, an exempt constexpr
variable initializer, and a member-function static local. Optional `stdout.txt`
in the case runner compiles and runs both original and rendered sources under
available g++/clang++, checking each against the same explicit output oracle.
Core unit tests independently exercise backward lexical/AST/system paths,
transitive aliases, unions, dependent names, associations, report attribution,
protocol round trips and unbounded-dispatch refusals. Extractor model tests cover
uninstantiated patterns and inert controls, including function/member static locals
and out-of-line members.

Initial branch validation (before this refinement): Release LLVM-free CMake/Ninja build with `-j4`; all six
available CTest suites passed under 120-second test timeouts. All 33 pruning core
unit tests passed, including the exhaustive graph oracle. All nine new original
fixtures matched their output oracles with both g++ and clang++ (18 runs), with
20-second compile and five-second runtime limits. `git diff --check` passed.
The required LLVM 23.1.5 SDK is absent locally; the installed LLVM tree also lacks
Clang AST development headers. Extractor compilation, worker extraction/model
cases and original-versus-rendered runtime comparisons remain unverified. They
require the repository's pinned SDK; no alternate LLVM version was substituted.

## Refinement probes and regression coverage

The reproducible probes are in
[`tests/probes/instantiation_effects`](../../tests/probes/instantiation_effects/run.py).
Run `timeout 120s python3 tests/probes/instantiation_effects/run.py` from the
worktree. Each compilation has a 20-second deadline and each executable a
five-second deadline; all outputs go under `.checks/probes`. Results below are
from GCC 15.2.0 and Clang 22.1.8, C++20, on AArch64 Linux.

| Probe | GCC | Clang | Consequence |
| --- | --- | --- | --- |
| Friend has fixed `int` return, self by value / `const&` / pointer; namespace redeclaration; dormant `sizeof(C<int>)` before address-taking | All three link and print `present` with seed; all three fail to link without seed | Same | None of these parameter shapes alone is exempt |
| Same address-taking with deduced `auto` return | Rejects with and without seed | All three work with seed; reject without | Do not rely on return deduction to repair missing instantiation |
| ADL requires-expression on `C<X>*` or `*p`, forward declaration only | Both checks false | Both checks false | Pointers/references alone need no complete object |
| Same ADL probes with reachable class definition for a fresh specialization | Both checks true | Both checks true | ADL can itself instantiate the carrier |
| Parenthesized call suppressing ADL, namespace redeclaration with deduced return, in a dependent requires-expression | Prints `0` without seed, `1` with seed | Rejects without seed; prints `1` with seed | Ordinary lookup is a separate route; requires-expressions do not rescue the exemption |
| Class parameter object with constexpr destructor calling `puts` only outside constant evaluation | Prints only `main` | Same | Observation is insufficient for a cross-implementation destructor exemption |
| Non-constexpr destructor / constexpr destructor with forbidden constant-evaluation mutation | Both rejected | Both rejected | Constant destruction is enforced, but it does not forbid runtime branches |
| Scalar-only vs class-selected user branch (`unseen_argument.cpp`) | Both builds pass | Both pass; AST confirms extra class-valued argument only in class-selected build | Known specializations are not a completeness bound |

The decisive friend counterexample is small:

```cpp
template<class T> struct C { friend int supplied(C) { return 7; } };
int supplied(C<int>);
struct Dormant { decltype(sizeof(C<int>)) declaration(); };
auto *volatile address = &supplied;
```

Deleting `Dormant` leaves the namespace declaration visible, but loses the
instantiation supplying its inline definition. The pointer initializer odr-uses
the function without a call or argument-dependent lookup. It links only with
`Dormant` under both compilers. Replacing both parameters with `C const&` or
`C*` gives the same result. This follows the distinction between declaration
and definition instantiation in [temp.inst]/3 and /5
(https://timsong-cpp.github.io/cppwp/n4861/temp.inst), plus the ODR definition
requirement; ADL's associated-class rules do not apply to `&supplied`.
Consequently no parameter-shape friend exemption is adopted. A stronger
whole-translation-unit proof excluding all possible redeclarations could be a
future refinement, but checking only Clang's known redeclaration chain would
miss dependent declarations instantiated differently on the judge.

New pruning fixtures cover scalar aliases, the generic-lambda scalar NTTP,
enums, class pointers/references, dependent pointer shapes, provably trivial
class objects, nontrivial class objects, system-selected scalar/class types
(including a field), placeholder/dependent controls and all three friend
address-taking counterexamples. The original nine effect fixtures remain.
Extractor model cases independently inspect effect classification.

The three pipeline report oracles now expect `D3d-5`. `github-issue8` deliberately
retains C7: its specialization contains the identifier `C`, which the conservative
potential graph connects to `test14::C`. That concept has a dependent NTTP type
`typename S<T>::type`; this refinement does not prove arbitrary dependent member
types invariant across targets. Family unions then retain the C7 primary, both
specializations and the out-of-line member. This is conservative lexical
retention, not an independent effect in C7; the caide oracle and port note now
say so. No retention rule is weakened just to restore the old output.

## Refinement measurement

The public atl-cpp checkout was cloned inside `.checks/atl-cpp`, at
`97d9e2365d265b293d430ad0b2d69dd5ebe684ab`. A checksum-verified public
LLVM 23.1.5 revision-1 Linux ARM64 SDK was downloaded inside the worktree.
The Release worker and CLI built with Clang 22.1.8 and `-j2`; the earlier GCC
Release worker compilation was killed for memory exhaustion. No alternate SDK
version was substituted.

Both sides used this command (with the corresponding CLI/worker binaries):

```sh
timeout 180s distill --judge cses --strip-comments --no-line-markers \
  --report report.txt -o output.cpp g++ -std=c++23 -O2 -DNDEBUG \
  -I .checks/atl-cpp/include .checks/atl-cpp/docs/examples/EXAMPLE/quick.cpp
```

| Example | Original PR branch, bytes | Refined branch, bytes |
| --- | ---: | ---: |
| online-convolution quick | 149,050 | 149,050 |
| polynomial-gcd quick | 157,713 | 157,713 |

The outputs remain byte-identical: the type fixes remove false effect sources,
but the remaining effects still reach the same retained families. In the online
report, `mod_int32`, `mod_int64`, `convolution_uint64` and `online_product_state`
change from intrinsic parameter-object sources to possible triggers of
`mod_int`. The lambda NTTP inside `convolution_uint64` is
`[&]<uint32 P>(std::integral_constant<uint32, P>)`; the outer constrained
parameters really are type parameters. `online_product_state` has the enum NTTP
`online_product_algorithm Algorithm`, not a class object. The
`modulus_engine` placeholders and `mod_int`'s dependent parameter still remain
conservative, along with function friends. Thus this refinement does **not**
recover the pre-D3d-5 size reduction; doing so needs a stronger proof than
known scalar instantiations or the proposed friend parameter shapes.

These are CSES-profile, **local** builds. The host is AArch64 Linux with GCC
15.2.0; the profile's x86-64 GCC 13 compiler is unavailable, and the report
correctly skips the incompatible local compiler as a judge checker. Original,
original-PR pruned and refined sources for both examples were separately
compiled and run under g++ and clang++ (12 compile/run comparisons), all matching
the repository's `quick.out` byte for byte. An actual CSES run and a complete
atl-cpp sweep remain unverified.

## Final refinement validation

* Release CLI/worker compilation succeeded against the pinned LLVM 23.1.5 SDK.
* All 54 pruning fixtures passed, including the original nine effect regressions
  and the three new fixtures, with original/pruned g++ and clang++ runtime oracles.
* All 100 extraction model cases passed (707 assertions), including scalar,
  dependent, class, alias and selected-member controls.
* All 81 caide cases passed; `github-issue8` passed again with the final worker.
* Seven core/resolver fixture suites passed. The remaining worker/CLI suites
  passed except the pre-existing host-specific `resolver_differential` check
  described below. Three pipeline suites initially hit their 120-second CTest
  deadlines under `-j4`; bounded sequential retries passed: `pipeline_cli`
  48/48 (102.1 s), `pipeline_pruning_d3c` 21/21 (53.9 s), and
  `pipeline_cli_d3c` 14/14 (81.2 s). Both D3d pipeline suites also passed.
* All 44 standalone compiler probe combinations completed with the expected
  results; negative compilations are intentional and their diagnostics are saved.
* Both final measurement outputs were byte-identical to the original branch's
  outputs; the 12 example compile/run comparisons matched their output oracles.
* `git diff --check` passed. No commit or push was made.

The remaining failure is `resolve_fake_external_model_and_errors` in
`resolver_differential`: `IntegerModel{}` requests signed 32-bit `wchar_t`, while
this AArch64 GCC uses unsigned 32-bit `wchar_t`. The GCC oracle cannot represent
that model and refuses; the Clang evaluator can represent it, so differential
comparison fails. An isolated rerun reproduced the failure. The resolver test,
GCC oracle and Clang evaluator are unchanged from the original PR branch. This
is separate from the policy-string expectations and requires a suitable host or
an independent test/oracle fix. Native clang resolver tests passed. Unsupported
local library features still report their existing explicit skips.
