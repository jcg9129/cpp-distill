# Implicit instantiation effects — 2026-11-20

Current policy: **D3d-7**, under the extended A11 approved by the user on
2026-11-20. The constexpr and block-storage exemptions are adopted.

## D3d-7 rule

Rule-1 storage roots are unchanged. The instantiation-effect classifier excludes
constexpr variables of **any scope/type**, including dependent declarations and
lifetime-extended temporaries, and **all block-scope static/thread variables**.
This changes reverse retention of possible instantiation triggers. Ordinary
name/type/value dependencies, whole-record retention, attribute/symbol effects,
family unions and the unsupported-construct gates continue to apply.

For non-constexpr template static/thread storage outside blocks, retain the
small positive proof from `db967a6`: the written pattern must establish constant
initialization, trivial destruction of the object's type on every accepted
target, and absence of lifetime-extended temporaries. Initialization uses the
existing syntactic `inertExpr` proof, never the local evaluator's answer.
Types must be non-dependent written scalars, arrays or non-template user
records with trivial destruction throughout their bases/fields. Written user
aliases are followed; deduced and template-qualified selections are refused.
Reference fields are harmless themselves, but `inertExpr` rejects materialized
temporaries and default member/default argument expressions outright, so it
cannot miss lifetime extension through them.
Implicit specializations use their pattern; explicit specializations retain
their own classification. Effects are accumulated and never cleared by a later
exempt variable in the same family.

Dropped from `db967a6`: `necessarilyDynamic` and its wrapper/operand analysis,
the conditional block-local proof, constexpr/constinit alternatives inside
the extra storage proof, and the separate extended-temporary walk (the remaining
syntactic initializer proof already rejects those expressions). The blanket A11 block exemption subsumes the dynamic
proof; constexpr variables bypass it entirely; the independent attribute gate
already refuses `constinit`. No rule-1 exemption was widened.

Dynamic namespace-scope template storage and non-constexpr constant-initialized
namespace storage with possible non-trivial destruction remain sources. So do
uncertain dependent/type-selected namespace declarations. Non-constexpr
reference/aggregate lifetime extension remains conservative. The A11 temporary
exclusion applies to temporaries whose lifetime **constexpr variables** extend,
not every extended temporary.

## Soundness under A1–A11

The existing §4.7 argument still applies under its assumptions (A1–A7, A9 and
A11; no new A8/A10 assumptions). Reverse closure over **all** potential user
families retains every possible path to a remaining effect source. It includes
identifier-token/AST references, dependent call names, associations, system
entity edges and unions. It does not treat locally observed specializations as
an exhaustive list. Ordinary forward closure then preserves the surviving
triggers' definitions and dependencies. The four-million-edge limit and
unbounded incoming library-hook refusals remain in force. Hidden friends still
need the same-record carrier certificate when they can reach a remaining
storage/attribute effect: excluding friend injection does not exclude those
effects. Unsupported ownership, attributes or unbounded traversal still refuse.

A1 bounds header-introduced names; A2 and macro/paste checks preserve identifier
carriers; A3–A5 and A9 justify preprocessing facts. A6 bounds library hook names,
not all incoming instantiations. A7 excludes implementation-limit/cache effects
of constant evaluation. A11 excludes both compilation-success and observable
behavior dependencies on friend-definition instantiation, and the specified
destruction effects. The exact adopted text is in [design.md](../design.md).

Constant initialization has no independent runtime initialization effect.
For the remaining namespace storage exemption, trivial destruction and absence
of extended temporaries rule out independent termination effects across targets.
For constexpr variables, A11 instead supplies the destruction exemption.
Block-storage dynamic initialization requires control to reach the declaration;
ordinary dependencies preserve executed functions and their declarations.
A11 supplies the exemption for destruction of never-reached block storage,
including its constant-initialized cases. Reached block storage retains its
initialization/destruction through the unchanged forward traversal and roots.

The compiler probes do **not** establish a language guarantee that a
constant-initialized block object escapes destruction when never reached.
[stmt.dcl] conditions dynamic initialization on the first passage through its
declaration; [basic.start.static] separately requires constant initialization,
and [basic.start.term] provides destruction and ordering requirements for
constructed objects. Construction can complete without a runtime constructor
call. See [N4861 stmt.dcl/4–5](https://timsong-cpp.github.io/cppwp/n4861/stmt.dcl#4),
[basic.start.static/2](https://timsong-cpp.github.io/cppwp/n4861/basic.start.static#2),
[basic.start.term/1–3](https://timsong-cpp.github.io/cppwp/n4861/basic.start.term#1)
and [dcl.init/21](https://timsong-cpp.github.io/cppwp/n4861/dcl.init#21).
The corresponding C++17/C++23 wording was examined in the prior investigation;
CWG 2123 is not a resolved never-reached destruction guarantee.

## Probes and pruning regressions

The retained [probe README](../../tests/probes/instantiation_effects/README.md)
maps each source to its A11 clause and supplies bounded commands. Observations
are local AArch64 Linux, GCC 15.2.0 and Clang 22.1.8; GCC 13 is unavailable.

| Probe | GCC 15 | Clang 22 | D3d-7 treatment |
| --- | --- | --- | --- |
| Fixed-return friend address, self by value/reference/pointer | Links with dormant carrier; fails without | Same | A11 friend-definition exclusion |
| Deduced friend return/lookup | Some redeclarations rejected; lookup can change | Lookup/compilation can change | A11 covers compilation success too |
| Class template parameter object | Only `main`; invalid constant destruction rejected | Same | A11 parameter-object destruction exclusion; observations alone are insufficient |
| Constexpr reference to a non-trivial temporary's member | `main`, then `destroy`; dropping trigger loses destruction | Unsupported constant expression | A11 constexpr extended-temporary exclusion |
| Constexpr aggregate with reference member | Same, despite trivial aggregate destructor | Unsupported constant expression | Same exclusion |
| `Pick<(sizeof(long)==4)>` reference/aggregate | Trivial selection quiet; noisy selection destroys | Trivial accepted; noisy unsupported | A11 exclusion, not a cross-target triviality proof |
| Never-reached constant-initialized block static/thread object, template/non-template, C++17/20/23, `-O0`/`-O2` | No destruction | No destruction | A11 never-reached block destruction exclusion |
| Same block object, reached twice, ordinary destructor | Destroys once | Destroys once | Forward dependencies preserve reached declaration |
| Same block object, constexpr destructor (C++20/23) | Destroys once when reached | Const object destructor folded away even when reached | No portable exemption follows from this difference |

The block matrix also has `constinit` controls in C++20/23 and saves assembly:
GCC guards destruction registration (`__cxa_atexit` / `__cxa_thread_atexit`)
on first passage; Clang does so for ordinary destructors but elides registration
for these const constexpr-destructor controls. No tested never-reached form
has startup registration. The matrix has 144 configurations / 288 compile-run
comparisons, plus assembly compilation. The other matrix has 64 combinations.

A11 fixtures explicitly require the report line `pruning assumption: A11` and
allowed removal of `dormant`. `a11-exclusion | explanation` runs the original
oracle and omits the equivalence assertion excluded by A11. Coverage includes
Pick, constexpr destruction, constexpr aggregate lifetime extension and
non-constexpr constant-initialized block storage. Exact constexpr Noisy
extended-temporary examples stay in the standalone probes because Clang cannot
parse them. Non-constexpr namespace lifetime-extension fixtures still require
retention and original/rendered equality. Dynamic namespace template storage
and constant-initialized namespace storage with non-trivial destruction are
also retention regressions. Extractor tests independently check dependent,
thread-storage and target-selected cases and unchanged rule-1 roots.

## Measurements

Pinned atl-cpp revision: `97d9e2365d265b293d430ad0b2d69dd5ebe684ab`.
The public checkout and all outputs stay inside `.checks/`. The checksum-verified
LLVM 23.1.5 revision-1 Linux ARM64 SDK has archive SHA256
`5ca712f18c710592a8fa89758246a6b6cbdbd16c1fbd5f12c7f5bcdf35fe06de`.
Release uses Clang 22.1.8 and `-j2`. Acquisition/configuration/build have hard
outer timeouts of 240/60/600 seconds; distillation 180 seconds, each example
compilation 120 seconds and execution five seconds. SDK/build/check artifacts
are deleted after validation.

```sh
timeout 180s build/distill --judge cses --strip-comments --no-line-markers \
  --report report.txt -o output.cpp g++ -std=c++23 -O2 -DNDEBUG \
  -I .checks/atl-cpp/include .checks/atl-cpp/docs/examples/EXAMPLE/quick.cpp
```

| Policy | online-convolution quick, bytes | polynomial-gcd quick, bytes |
| --- | ---: | ---: |
| PR #104 / D3d-6 (`1715f38`), historical | 149,050 | 157,713 |
| Initial `db967a6` positive proofs, historical | 149,050 | 157,713 |
| D3d-7, extended A11, freshly verified | 84,539 | 99,721 |

The prior constexpr-only and block-only comparisons individually saved zero
bytes: each left sources that reached the same families. Together the exemptions
remove `atl::static_modulus`'s `static constexpr value_type mod = Mod;`,
`atl::montgomery`'s `mod`, `mod_inv`, `r_squared`, `twiddles<Z>`'s
`static constexpr U mod = Z::mod();`, and `get_twiddles<Z>()`'s block-local
`static typename scalar_ntt::twiddles<Z> const t;` from effect-source classification.
Fresh reports have zero `family ... instantiation effect: possible trigger of X`
lines, including all three groups (`static_modulus`, `montgomery`, `scalar_ntt`).
Counts report one attributed cause, not independently necessary declarations.
The savings from PR #104 are 64,511 and 57,992 bytes, respectively.

Both original and pruned artifacts compiled and matched their example's
`quick.out` byte for byte under GCC 15.2.0 and Clang 22.1.8 (eight comparisons).
Both distillation reports select `pruned (local)`, list policy D3d-7 and report
A11. The pipeline did not request runtime checks; the independent bounded
compile/run comparisons supply that evidence. These are local AArch64 checks,
not a check on CSES's x86-64 GCC 13 judge.

## Validation

The checksum-verified pinned-SDK Release build and final rebuild passed with
`-j2` and hard outer timeouts (600 seconds initially, 300 for the final rebuild).
All 108 extractor model cases passed, with 848 checks and zero failures.
The 64 friend/parameter-object/lifetime compiler combinations and 144 block
configurations (288 compile/run comparisons plus assembly builds) passed.

All **293 Python tests** passed: extensions 18, profiles 66, drift 55,
release 20, retry 23, matrix 11, library 84, LLVM CI 10 and measurement 6.
The missing pytest dependency was installed in an isolated environment inside
`.checks`, using the repository's pinned test requirements. Each suite had a
hard 120-second timeout (300 for library).

The final full CTest run (`--timeout 120 -j2`, 1800-second outer limit)
passed **102 of 103 suites** in 215.05 seconds, including all 63 pruning
fixtures, 108 extractor model cases (848 checks), all 81 caide fixtures,
unit tests and every pipeline/CLI suite. The sole failure is the pre-existing
AArch64 `resolver_differential` / `resolve_fake_external_model_and_errors`
mismatch: native GCC has unsigned `wchar_t` and cannot represent the requested
signed-wchar integer model, while Clang accepts it. Resolver/oracle/evaluator
files are unchanged from `1715f38`. The earlier sequential run exposed two new
fixture-oracle mistakes (record return-type association and a GCC-rejected
aggregate default-member form); the fixtures were corrected before this full
rerun. No implementation change was needed.

`git diff --check` passed. The SDK, build and `.checks` directories were removed.
The experiment branch was deleted after detaching its source checkout at
`7ed2870`, preserving its uncommitted diff and probe files.

## Brief history

D3d-5 introduced reverse retention of possible instantiation triggers. D3d-6
adopted the original friend-injection/template-parameter-object A11 clauses;
`1715f38` also repaired LLVM 23's invalid default `NestedNameSpecifier` sentinel
by explicitly using `std::nullopt`. The initial D3d-7 work at `db967a6` added
positive lifetime-safe storage proofs. The subsequent constexpr and block-scope
experiments established the additional assumption boundary and the combined
size reduction. On 2026-11-20 the user approved those A11 clauses; D3d-7 now
adopts both and removes superseded classifier logic. No experiment-only policy
is left in the implementation.
