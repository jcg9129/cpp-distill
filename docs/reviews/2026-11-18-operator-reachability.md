<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Review: removing non-member operators with unreachable parameter classes

Date: 2026-11-18. Requested by the user; implementation conditional on this
review finding a sound rule.

**Verdict: the proposed rule is unsound under A1–A7. Stop after phase 1.**
An operator need not be selected to affect the program. Its presence can make
overload resolution ambiguous inside a requires-expression. Removing that
operator can make a previously false requirement true, even when neither the
expression nor any retained declaration names its parameter class. Converting
constructors declared only inside that class suffice. Both installed compilers
agree on the counterexample; it needs neither resource exhaustion nor a
Clang/GCC disagreement, so A7 does not exclude it.

## Blocking counterexample: an unselected candidate changes a constraint

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
namespace m { struct Tag {}; }
namespace n {
struct Live : m::Tag {};
#ifndef REMOVE
struct Dead { Dead(Live); };
int operator+(Dead, Dead);
#endif
}
namespace m {
struct Other { Other(n::Live); };
int operator+(Other, Other);
}
template<class T> concept Addable = requires(T a) { a + a; };
int main() {
  (void)sizeof(m::Other);
  std::printf("%d\n", int(Addable<n::Live>));
}
```

Save as `build/operator-reachability-review/adl-ambiguity.cpp`, create
`build/operator-reachability-review/tmp`, and run from the worktree root:

```sh
export TMPDIR="$PWD/build/operator-reachability-review/tmp"
g++ -std=c++20 -O0 -pedantic-errors build/operator-reachability-review/adl-ambiguity.cpp -o build/operator-reachability-review/original
build/operator-reachability-review/original
g++ -std=c++20 -O0 -pedantic-errors -DREMOVE build/operator-reachability-review/adl-ambiguity.cpp -o build/operator-reachability-review/removed
build/operator-reachability-review/removed
```

The guard is only a way to compare the original and the proposed deletion.
The pruning input would be the preprocessed original, without those user
conditional directives; a directive-pinning rule does not address the example.

Observed with GCC 15.2.0 and Clang 22.1.8 on AArch64:

| Compiler | Original stdout | After deleting `Dead` and its operator |
| --- | --- | --- |
| GCC | `0` followed by newline | `1` followed by newline |
| Clang | `0` followed by newline | `1` followed by newline |

All variants are accepted with `-pedantic-errors`, in C++20 and C++23, at
`-O0` and `-O2`. All 16 compile-and-run checks passed exact output assertions.
The constructors and operator functions need no definitions: they are only
considered in an unevaluated expression, never executed.

The mechanism is standard overload resolution:

1. `n::Live` associates namespace `n`, and its complete base `m::Tag`
   associates namespace `m`. ADL therefore finds both namespace operators,
   even though neither has a parameter of type `Live`. Associated namespaces
   describe where lookup searches, rather than a parameter-type filter. See
   [basic.lookup.argdep](https://timsong-cpp.github.io/cppwp/n4950/basic.lookup.argdep).
2. Both candidates accept each operand through a user-defined conversion:
   `Dead(Live)` or `Other(n::Live)`. Neither sequence wins, so the expression
   is ambiguous. The conversion constructors need only be declared. See
   [class.conv.ctor](https://timsong-cpp.github.io/cppwp/n4950/class.conv.ctor)
   and [over.best.ics](https://timsong-cpp.github.io/cppwp/n4950/over.best.ics).
3. An invalid expression formed by substitution makes the requirement false,
   without making the program ill-formed. Deleting the `n` candidate leaves
   the `m` candidate viable and makes the requirement true. See
   [expr.prim.req](https://timsong-cpp.github.io/cppwp/n4950/expr.prim.req).

This disproves the proposed rationale's implication that a semantically
relevant operator must produce a retained reference to its parameter class.
An unsuccessful overload set has observable consequences too.

## Why the existing closure does not prove this candidate unnecessary

The following is an analysis of `prune_extract.cpp` and `computeRetention`,
supported by the local Clang AST dump. It is not a pinned-worker extraction
measurement: LLVM 23.1.5 was not built or run.

- `main` retains `Other`, `Live` and `Addable` by their lexical names and AST
  dependencies; retaining `Live` retains its base `Tag`.
- The association rule retains `m::operator+` through `Other`. Its signature
  mentions `Other`, not `Dead`. The two namespace overload sets are separate
  families, so family aggregation does not retain `n::operator+` through it.
- `n::operator+` has a single declaration and both parameters have type
  `Dead`. It qualifies even if the proposal requires **all** parameters to
  have the removed class type. Its outgoing edges retain `Dead` only if the
  operator itself is retained. `Dead`'s constructor refers to `Live`; the
  dependency runs from `Dead` to `Live`, not in the reverse direction.
- The concept's source mentions only `T` and `a`; no retained source names
  `Dead`. An instruction to reject removal when the class is named in
  requires-expression text would not catch this example.
- In the local Clang AST, the concept pattern contains a dependent
  `BinaryOperator '+'` with two `DeclRefExpr` operands. It contains no
  `UnresolvedLookupExpr` for the operator: ordinary lookup has no candidates
  at the concept definition. `VisitUnresolvedLookupExpr` therefore does not
  add `operator+` to `dependentNames`. There is no selected operator or
  constructed `Dead` object to retain through an expression edge. By
  contrast, a control with an ordinary-lookup operator produced a
  `CXXOperatorCallExpr` and an `UnresolvedLookupExpr`; the current dependent
  name fallback would conservatively retain operators in that control.

The AST observation can be reproduced with:

```sh
clang++ -std=c++20 -pedantic-errors -Xclang -ast-dump -fsyntax-only build/operator-reachability-review/adl-ambiguity.cpp > build/operator-reachability-review/adl-ambiguity.ast
```

The current unconditional operator root protects the example. Replacing it
with a conditional root on `Dead` permits the operator/class cycle to remain
unreachable, while `Other` independently retains the competing operator. A
least fixpoint, including the proposed conditional decision inside that
fixpoint, does not repair this missing semantic obligation. Computing the
closure with unconditional operator roots first and then deleting operators
is also not a proof: the roots themselves retained their classes.

An LLVM-free, hand-built `PruningModel` linked against the current core
confirmed that closure calculation. With both `nonMemberOperator` flags set,
both operators and both parameter classes survive. With those flags cleared,
the existing class-to-function associations implement exactly the proposed
conditional roots for this model: `Other` and `m::operator+` survive, while
`Dead` and `n::operator+` are removed. All expected fates and the absence of
disabled reasons were asserted. This checks the core closure argument; the
model's AST edges were supplied by hand, not extracted by the worker.

## Other requested attacks and controls

- **Conversions and cv/ref.** Changing only the `n` operator's parameters to
  `const Dead&` preserves the output change under both compilers. Stripping
  cv/ref does not close the hole. Making `Dead(Live)` explicit makes the
  original requirement true as well, confirming the converting-constructor
  mechanism. This control is not a proof that every class without such a
  constructor is safe.
- **Pointers.** Do not extend a future proof by simply stripping pointers:
  a null pointer constant can bind a `Dead*` parameter without an operand
  naming `Dead`, and a pointer does not require a complete class definition.
  Other parameter associations may save particular declarations, but that
  requires its own argument. The blocking example already uses class values
  and needs no pointer stripping.
- **Templates.** A plain deduced `operator+(wide<T>, wide<T>)` cannot infer
  `T` by first applying the converting constructor. A control replacing
  `Dead` with a class template and using
  `template<class T = int> int operator+(Dead<T>, Dead<T>);` consequently
  prints `1` before and after deletion under both compilers. This deduction
  control does not cover all operator templates. A blocking variant uses
  `template<class T> struct Dead { Dead(Live); };` and
  `template<class = void> int operator+(Dead<int>, Dead<int>);`, and changes
  the `m` operator to `template<class = void> int operator+(Other, Other);`.
  Both compilers then print `0` before and `1` after deletion. Specific class
  specializations in function templates are not sufficient evidence of
  unreachability. Generic `T`, `auto`, dependent aliases and unknown patterns
  would need to retain the unconditional root in any future implementation.
- **Rewritten candidates.** Change both operator declarations to return
  `bool` and spell `operator==`, then probe `a != a`. Both compilers still
  print `0` before and `1` after deletion: equality candidates participate
  through C++20 rewriting. Reverse/synthesized candidates, including those
  from `<=>`, must be included in any future candidate-preservation proof;
  looking only for references to the written operator name is insufficient.
  See [over.match.oper](https://timsong-cpp.github.io/cppwp/n4950/over.match.oper).
- **Stream insertion and standard customization.** Add `<ostream>`, change
  the `n` declaration to
  `std::ostream& operator<<(std::ostream&, Dead);`, change the `m`
  declaration analogously for `Other`, and probe
  `requires(std::ostream& out, T a) { out << a; }`. Both compilers again
  print `0` before and `1` after deletion. The stream parameter is a system
  type and does not itself retain either user parameter class. Thus even
  the requested unused-class stream-removal case needs a restriction beyond
  the presence of a removed class parameter. In the ordinary successful
  insertion case a retained user argument class or selected conversion can
  retain the operator, but that does not justify deleting failed candidates.
- **SFINAE and other unevaluated contexts.** Successful `decltype` or
  `sizeof` expressions may provide a selected-candidate edge. Substitution
  failure can instead affect overload availability without one. The
  requires-expression counterexample suffices to reject the shared premise;
  scanning only explicit mentions of `Dead` in constraint text cannot fix it.
- **Hidden friends, mixed families and other roots.** Hidden friends retain
  their existing whole-record ownership; this example uses only namespace
  functions. Family unions can also include shared declarators, members,
  specializations or other functions. A future exception must inspect every
  declaration in the entire union and preserve all independent root reasons.
  Literal operators and allocation/deallocation operators must not acquire
  an exception by accident; they do not share the proposed binary-operator
  argument. None of those additional cases is needed for this failure.
- **A6 and A7.** `data/names/adl.txt` explicitly leaves operator functions
  to the separate operator/association rules. A6 permits the operator route
  from standard-library code; it does not certify any parameter-based
  exception. The example's probe is user code and both compilers agree. A7
  excludes limit/cache-sensitive programs, not ordinary lookup ambiguity.
  The earlier constexpr review's resource-based divergence is therefore
  unnecessary here. A clean parse or a later successful GCC syntax check
  cannot catch this output change; both versions compile successfully.

The six variant/control pairs above (const-reference, explicit-constructor,
deduced-template, fixed-specialization template, rewritten inequality, stream
insertion) were each compiled and run under GCC and Clang in C++20 at `-O0`
with `-pedantic-errors`: 24 checks with exact output assertions, in addition
to the 16 checks for the primary example. No `<=>` execution probe or
pinned-worker regression is claimed.

## Decision and possible follow-up

Stop at phase 1 rather than approve an unproved stronger exception. Requiring
all parameters to name removed classes is already disproved above. Excluding
class names explicitly present in requires-expression text is also disproved.
Forbidding foreign-type converting constructors and excluding pointers could
be part of a narrower proposal, but it would still need a proof covering
source conversion functions, bases, dependent specializations, deduction,
lookup and failed-candidate effects. This review does not approve that rule.

A separate conservative direction would preserve operator names from every
dependent operator expression, including AST `BinaryOperator` nodes with no
ordinary-lookup candidates, before considering conditional operator roots.
That would retain this example's `n::operator+` without relying on viability.
It needs an audit of unary/binary/reversed operators, failed requirements,
SFINAE and system traversal; adding that fallback alone is not established
here as a complete soundness proof. It may also retain much of the motivating
library. A performance estimate cannot replace that audit.

Per the user's phase gate, there is no implementation, policy bump, design
amendment, phase-2 extraction/unit test change or runtime fixture. The
unconditional operator roots remain in force. The sources and binaries for
the review experiments live under the ignored worktree-local `build/` tree.

## Verification

- GCC 15.2.0 and Clang 22.1.8: all 40 compile-and-run assertions described
  above passed. Compiler temporary files were confined to the worktree via
  `TMPDIR`; no constexpr-limit options were used.
- The local Clang AST dump confirmed the ADL-only dependent binary expression
  and the ordinary-lookup control described above. This is evidence about the
  installed Clang, not a claim to have exercised the pinned extraction worker.
- The LLVM-free core closure experiment passed with the current roots and
  the proposed conditional roots, using a hand-built model as described above.
- Configured an LLVM-free Debug build with `DISTILL_LLVM_DIR` empty,
  `DISTILL_PROFILE_ONLY=OFF` and `CMAKE_CXX_FLAGS_DEBUG='-O0 -g0'`;
  `cmake --build build -j2` passed. No LLVM build was performed.
- `ctest --test-dir build --output-on-failure --timeout 120 -j2` passed all
  six LLVM-free suites (unit, inline cases, resolver cases, pipeline cases,
  checker models and caide assertion tokens). The focused unit invocation
  `build/tests/unit/distill_unit_tests --filter prune_` also passed all 27
  pruning tests with no failed checks. Worker extraction tests require the
  SDK worker and were not run.
- Whitespace validation of the new review passed. Git status contains only
  this review outside ignored build outputs. No commits or credential
  searches were performed; all generated files remained inside this worktree.
