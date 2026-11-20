<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# caide assertion port (D4a)

Reviewed individually against `/workspace/caide-cpp-inliner/tests/cases/<case>/etalon.cpp` and its numbered inputs on 2026-11-16. Source hashes remain unchanged. Assertions use preprocessing-token sequences, so comments and formatting cannot satisfy a removed-name check.

Each line classifies declaration retention/removal relative to caide. “Same removal” includes cases with no removed declarations. Unsupported legacy test-harness controls are documented explicitly; they are not compiler options and are not silently executed. Their input still receives the normal v8 checks, with the successful outcome asserted below. Actual unsupported inputs assert exit 1 plus diagnostics.

The unchanged c++2c commands require a GCC supporting that dialect. A missing dialect is an explicitly matched probe/baseline exit-1 outcome; pack indexing additionally permits its specific unsupported `args` expression diagnostic. Successful builds must return flat with the unsupported-A6 reason. No extraction or validation failure is accepted as a feature skip.

The caide runner shares `expect.txt` key=value syntax with pipeline cases. `contains[.N]` / `absent[.N]` compare whole token sequences; `reason` / `report.N` search full diagnostics/report. `exit`, `artifact`, optional `prune=no`, `keep` (indexed owner), `strip=yes`, `sample.N`, `runtime`, and `baseline-feature[.N]` state each adapted invocation. No byte-equality or size heuristic is used. CTest registers all 81 separately with a 120-second deadline in the resolver SDK job.

- `actually-written-type` — keeps more because §4.7 template-family aggregation retains every E specialization and hence Traits2 (caide removes the unused E<int> specialization).
- `aggregate-init-fields` — keeps more because §4.7 A6 has no c++2c audit: flat retains unused_pair along with all point3 fields.
- `alias-in-template-argument` — same removal: no eligible removals; spelled aliases X/Y and template f/C remain (§4.7 rules 2/4).
- `base-class-of-template` — keeps more because §4.7 template-family aggregation retains templ<double> and its unused_base dependency.
- `base-initializers` — same removal: no eligible removals; Type in the base initializer retains the alias and A (§4.7 rules 2/4).
- `caide-concept-comment` — keeps more because §4.7 keeps A whole, including g; caide concept comments have no special meaning; unused B still goes.
- `compiler-macro-branch` — same removal: both compiler branches remain; §4.5 leaves __clang__ unknown without a profile, disabling pruning.
- `concept-constrained-ctad` — keeps more because §4.7 A6 has no c++2c audit: flat retains splitter as well as the combiner constraint and deduction guide.
- `concept-ctor-requires` — keeps more because §4.7 A6 has no c++2c audit: flat retains value_range with index_range and the constrained constructor.
- `concepts-constraints` — same removal: Subtract goes; Add/Add2/Add3/Concept4 and both AlwaysTrue constraints remain (§4.7 rules 2/4).
- `cpp23-deducing-this` — keeps more because §4.7 A6 has no c++2c audit: flat retains Unused and the Counter record.
- `cpp23-if-consteval` — keeps more because §4.7 A6 has no c++2c audit: flat retains unused and both if-consteval branches.
- `cpp23-multidim-subscript` — keeps more because §4.7 A6 has no c++2c audit: flat retains deadFn and the multidimensional operator.
- `cpp23-static-operator-call` — keeps more because §4.7 A6 has no c++2c audit: flat retains Dead and the static call operator.
- `cpp26-pack-indexing` — keeps more because §4.7 A6 has no c++2c audit: flat retains unusedHelper; an unavailable GCC pack-indexing baseline is an explicit exit-1 outcome (§1.2).
- `delayed-parsing` — unsupported because the original command requests Clang-only -fdelayed-template-parsing; GCC rejects it before a baseline can build (§1.2).
- `enums` — same removal: Int1/UnusedEnum/UnusedEnumClass go; complete used enums, Int2 and f remain (§4.7 rules 2/4).
- `error-undefined-concept` — unsupported because this is a caide error fixture with undefined Integral/UnsignedIntegral; baseline failure is exit 1 (§1.2).
- `error-undefined-type` — unsupported because this is a caide error fixture with undefined NoSuchType; baseline failure is exit 1 (§1.2).
- `field-layout-sizeof` — same removal: none; Packet stays whole including its unnamed-by-use reserved field (§4.7 whole records).
- `friends` — keeps more because §4.7 keeps A whole and rule 2 retains all classes/functions named in its friend declarations.
- `gcc-autodetect` — keeps more because §4.7 never removes namespace using-directives; dead still goes and factorial remains.
- `gcc-bits-stdc++` — same removal: deadCode goes; using-directive, vector use and used remain (§4.7 rules 1/2/4).
- `gcc-builtins` — same removal: deadPopcount goes; countBits and the builtin call remain (§4.7 rules 2/4).
- `gcc-int128` — same removal: unusedBig goes; square and __int128 remain (§4.7 rules 2/4).
- `github-issue17` — same removal: none; function-type alias Func and value<int> stay reachable (§4.7 rules 2/4).
- `github-issue4` — same removal: none; A forward declaration/definition and member template stay together (§4.7 whole records).
- `github-issue8` — D3d-5 conservatively retains C7 and its specializations/out-of-line member: its nested `C` token can trigger `test14::C`, whose NTTP has a dependent member type. The used dependent/default/constraint paths also remain (§4.7 rules 2/4/6).
- `global-side-effect-ctor` — same removal: none; class-type boot is a root and retains Boot/state (§4.7 rule 1).
- `global-side-effect-roots` — keeps more because §4.7 rule 1 roots non-inert static/thread/template variables and the enclosing unusedFunction; StaticOwner stays whole.
- `ident-to-keep` — keeps more because §4.7 keeps C whole (method2 too); legacy member roots ns::C::C/method1 are explicitly mapped to indexed owner C.
- `include-option-std` — unsupported because §4.1 rejects -include, even for a system header (exit 1).
- `include-option-user` — unsupported because §4.1 rejects forced user -include (exit 1).
- `inheriting-ctor` — same removal: none; inherited A(int) and using A::A stay in whole records (§4.7 rules 2/4).
- `inliner1` — keeps more because §4.5 leaves external/integer conditions unknown without a profile and §4.7 preserves macros; pruning is disabled.
- `inliner2` — keeps more because §4.7 refuses non-standard system header myvector; flat retains unused declarations and record members.
- `inliner3` — keeps more because §4.7 preserves whole records/functions, shared declarations and macro invocations; rule 2 also retains global unused by the member name. Legacy caide keep comments are not roots.
- `line-directives` — unsupported because §1.2 rejects user #line (exit 1).
- `macros` — keeps more because §4.5 cannot resolve integer comparisons without a target model, so flat retains unused_func and all macro directives (§4.7).
- `merge-namespaces` — same removal: unused scalar, unused2 and Unused go; all used1/used2/used3 declarations remain; namespace merging is not required (§4.7 shells).
- `merge-namespaces-2` — same removal: none; both f declaration and definition stay; shell formatting is unconstrained (§4.7 family aggregation).
- `merge-namespaces-nested` — same removal: none; all four called functions retain their nested/plain namespace ownership (§4.7).
- `merge-namespaces-nested-then-plain` — same removal: none; inB/inA retain their distinct owners (§4.7 namespace shells).
- `namespace-alias-dangling` — keeps more because §4.7 A6 has no c++2c audit: flat retains geometry/twice and geo, so the alias does not dangle.
- `no-optimize` — same removal: none; legacy noOptimize.txt maps explicitly to --no-prune and retains unusedHelper (§2).
- `pull-headers-up` — same removal: none; §4.7 refuses non-standard system headers, retaining both includes and user declarations in place.
- `qualifiers` — same removal: none; base aliases, inherited using-declarations and F::i stay with their whole records (§4.7 rules 2/4).
- `record-layout-special-members` — keeps more because §4.7 keeps Layout whole, including unused(), all fields, special members, and their dependencies.
- `references-from-template-arguments` — same removal: none; spelled and instantiated template arguments retain a/b/c and A/B/C (§4.7 rules 2/4).
- `remove-comments` — unsupported because the original command requests Clang-only -fparse-all-comments; GCC rejects it (§1.2).
- `remove-namespaces` — keeps more because §4.7 keeps UsedClass whole, including the out-of-line UnusedMethod; unused namespaces/declarations still go.
- `remove-template-functions` — keeps more because §4.7 overload/template-family aggregation retains f1<int>; all f1/f2 families remain.
- `remove-type-alias` — same removal: TypeAlias and AliasTemplate go; only main remains (§4.7 retention closure).
- `repair-gives-up` — unsupported because caide forceRemove/verifyCommand repair metadata has no v8 counterpart (§4.6 no repair loop); normal checked pruning retains twice and removes unused.
- `repair-restores-function` — same removal: unused goes and twice remains by §4.7 rules 2/4; caide forceRemove/repair metadata is not a distill option (§4.6).
- `repair-restores-macro` — same removal: unused goes; SQ is preserved unconditionally as a source directive (§4.7); caide forceRemove is not applied.
- `repair-restores-overloads` — same removal: unused goes; all f overloads remain in the same family (§4.7); caide repair injection is not applied.
- `repair-restores-type` — same removal: unused goes; Point stays whole (§4.7); caide repair injection is not applied.
- `sizeof` — same removal: unusedVar/unusedType go; unevaluated usedVar/usedType remain (§4.7 rules 2/4).
- `source-ranges` — same removal: trailing-return f and noexcept g declaration/definition go (§4.7).
- `static-assert` — keeps more because §4.7 roots namespace static_asserts and preserves function bodies, retaining int2/c_zero and all assertions.
- `static-assert-branch` — keeps more because §4.7 preserves kept function bodies whole, including the braceless else static_assert.
- `std-namespace` — keeps more because §4.7 refuses non-standard system header mystd.h, preserving both namespace using-directives (including the local one).
- `stl` — same removal: none; compiler -v is unmodelled (§4.1) so raw preserves all standard includes and main.
- `strip-comments-all` — same removal: all comments go under explicit --strip-comments; the URL literal and url/main remain (§4.8).
- `template-alias` — same removal: none; Type/Alias and declval2/3/R2/R3 dependencies survive (§4.7 rules 2/4).
- `template-friend` — unsupported because the caide syntax-only fixture cannot link D<int>::f(); §1.2 requires an executable baseline (exit 1).
- `template-if-constexpr-member` — same removal: none; read_float survives uninstantiated if-constexpr lookup in the whole reader record (§4.7).
- `template-partial-spec-dependent` — same removal: none; rank partial specialization, rank_v and scalar_t remain through dependent constraints/types (§4.7 rules 2/4).
- `template-variables` — keeps more because §4.7 template-family aggregation retains every I3 specialization and hence S1; inert unused I1 still goes.
- `templated-context` — keeps more because §4.7 keeps Outer records whole (MoreInnerUnused), and template-family aggregation retains Outer<double>.
- `track-parent-decls` — same removal: none; F constructor and callback used survive lambda dependency traversal (§4.7 rule 4).
- `ull` — same removal: none; spelled ull alias remains (§4.7 rule 2).
- `unused-fields` — keeps more because §4.7 keeps A whole, including static i and its out-of-line shared definition.
- `using-base-grandparent` — same removal: none; whole base records retain helper and twice reached through dependent using-declarations (§4.7).
- `using-base-member-template` — same removal: none; §4.7 lacks a c++2c A6 audit, so flat retains storage_base members and every dependent using-declaration.
- `using-declarations` — unsupported because the caide syntax-only fixture cannot link ns3::used3(); §1.2 requires an executable baseline (exit 1).
- `verify-fallback-judge-fails` — unsupported because caide verifyCommand=false is not a distill configuration (§4.6); normal pruning keeps usedHelper and removes unusedHelper.
- `verify-fallback-judge-missing` — unsupported because caide verifyCommand is not a distill profile/check compiler (§4.6); normal pruning keeps usedHelper and removes unusedHelper.
- `verify-run-detects-difference` — same removal: none; greeting is already a class-type root (§4.7 rule 1), so unmodified output matches its sample; caide forceRemove is not applied (fault checks live in pipeline/).
- `verify-run-passes` — same removal: unused goes; scanf/printf main remains and both original samples compare (§4.6).
