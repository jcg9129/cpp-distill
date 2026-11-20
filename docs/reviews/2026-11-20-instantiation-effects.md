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
Conservatively, a non-type template parameter whose type fails the existing
positive syntactic scalar type whitelist also seeds reverse retention. This
includes `auto`, dependent and alias-selected types that could become class-valued
on the judge. The whitelist's accepted builtin/alias types cannot introduce such
objects. This deliberately also over-retains some pointer/reference/enum parameter
forms. A local probe with a constexpr destructor branching on constant evaluation
printed only `main` under both compilers; that observation is not used as a
cross-implementation proof. Unknown ownership of a materialized parameter object
still triggers the ordinary unowned-dependency refusal.

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

Local validation: Release LLVM-free CMake/Ninja build with `-j4`; all six
available CTest suites passed under 120-second test timeouts. All 33 pruning core
unit tests passed, including the exhaustive graph oracle. All nine new original
fixtures matched their output oracles with both g++ and clang++ (18 runs), with
20-second compile and five-second runtime limits. `git diff --check` passed.
The required LLVM 23.1.5 SDK is absent locally; the installed LLVM tree also lacks
Clang AST development headers. Extractor compilation, worker extraction/model
cases and original-versus-rendered runtime comparisons remain unverified. They
require the repository's pinned SDK; no alternate LLVM version was substituted.
