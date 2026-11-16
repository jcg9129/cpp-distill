<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# caide measurement corpus

Copied from the user's own GPL-3.0 fork of caide-cpp-inliner at
`/workspace/caide-cpp-inliner/tests/cases/*` on 2026-11-16 (81 case directories).
The upstream project is https://github.com/slycelote/caide-cpp-inliner.
These sources retain their original notices and are distributed under GPL-3.0;
see the repository's [LICENSE](../../../LICENSE). SHA256SUMS identifies the
byte-for-byte copied files; generated cmd.txt and main.cpp adapters are excluded.

This is measurement input only. No golden output or expected-result assertions
were imported; D4 ports those assertions. Error cases, unsupported options,
missing judge compilers and newer dialects can legitimately produce error/raw/flat
rows. Keep/force-remove/verification metadata is preserved for the later port but
is not interpreted by measurements. No corpus program is run by measurement.

The original layout has one or more numbered C++ source fragments per case.
For the four multi-fragment cases, a generated main.cpp includes them in numeric
order as one translation unit. All other cases use 1.cpp. Generated cmd.txt
starts with GNU C++20/O0, appends the original clangOptions.txt words (so its
explicit dialect wins), and replaces TEST_ROOT with the case directory `.`.
Forced includes remain unsupported by cpp-distill and are recorded as errors.
No flags or input source contents are silently repaired to obtain a smaller row.

## Expected outcome classes after D3d

These are review expectations for the unchanged adapters on Linux GCC with the
requested language features available, not a new SDK measurement or D4 golden
assertions. Compare case names with the `file` column of `ci/measure.py` output;
verification may be local. `pruned` means the checked pruning stage succeeds,
even if the conservative rules remove no bytes. Compiler/library versions can
add baseline or system-header parse failures; retain their full diagnostics.
The C++26 (`c++2c`) cases remain flat because A6 has no C++26 audit.

One adapter needs an additional class: `stl` passes `-v` **after** the compiler
name. Design §4.1 treats this unmodelled compiler option as raw-only. Its former
4 MiB raw-check error should disappear; expecting flat/pruned would require
changing that approved option policy or the adapter. The separate D3d STL
pipeline regression omits `-v` and requires both raw and flat checks to pass.

| Case | Expected class | Reason / retained contract |
| --- | --- | --- |
| `actually-written-type` | pruned | rules 1–5; checked build |
| `aggregate-init-fields` | flat-with-reason | unsupported A6 dialect `c++2c` (C++26) |
| `alias-in-template-argument` | pruned | rules 1–5; checked build |
| `base-class-of-template` | pruned | rules 1–5; checked build |
| `base-initializers` | pruned | rules 1–5; checked build |
| `caide-concept-comment` | pruned | rules 1–5; checked build |
| `compiler-macro-branch` | flat-with-reason | unknown `__clang__` conditional without a profile |
| `concept-constrained-ctad` | flat-with-reason | unsupported A6 dialect `c++2c` (C++26) |
| `concept-ctor-requires` | flat-with-reason | unsupported A6 dialect `c++2c` (C++26) |
| `concepts-constraints` | pruned | requires-expression parameters owned by their concept |
| `cpp23-deducing-this` | flat-with-reason | unsupported A6 dialect `c++2c` (C++26) |
| `cpp23-if-consteval` | flat-with-reason | unsupported A6 dialect `c++2c` (C++26) |
| `cpp23-multidim-subscript` | flat-with-reason | unsupported A6 dialect `c++2c` (C++26) |
| `cpp23-static-operator-call` | flat-with-reason | unsupported A6 dialect `c++2c` (C++26) |
| `cpp26-pack-indexing` | flat-with-reason | unsupported A6 dialect `c++2c`; baseline error instead if GCC lacks pack indexing |
| `delayed-parsing` | error-with-reason | GCC rejects clang-only `-fdelayed-template-parsing` |
| `enums` | pruned | rules 1–5; checked build |
| `error-undefined-concept` | error-with-reason | baseline: undefined `Integral` / `UnsignedIntegral` |
| `error-undefined-type` | error-with-reason | baseline: undefined `NoSuchType` |
| `field-layout-sizeof` | pruned | rules 1–5; checked build |
| `friends` | pruned | rules 1–5; checked build |
| `gcc-autodetect` | pruned | C++17 A6 supported; standard-header preprocess checks |
| `gcc-bits-stdc++` | pruned | C++17 A6 supported; standard-header preprocess checks |
| `gcc-builtins` | pruned | rules 1–5; checked build |
| `gcc-int128` | pruned | rules 1–5; checked build |
| `github-issue17` | pruned | rules 1–5; checked build |
| `github-issue4` | pruned | rules 1–5; checked build |
| `github-issue8` | pruned | rules 1–5; checked build |
| `global-side-effect-ctor` | pruned | rules 1–5; checked build |
| `global-side-effect-roots` | pruned | attribute and static/thread-storage roots retained |
| `ident-to-keep` | pruned | rules 1–5; checked build |
| `include-option-std` | error-with-reason | rejected `-include` |
| `include-option-user` | error-with-reason | rejected `-include` |
| `inheriting-ctor` | pruned | rules 1–5; checked build |
| `inliner1` | flat-with-reason | unknown external conditionals / integer model without a profile |
| `inliner2` | flat-with-reason | non-standard system header `myvector` |
| `inliner3` | pruned | rules 1–5; checked build |
| `line-directives` | error-with-reason | rejected user `#line` |
| `macros` | flat-with-reason | unknown integer comparisons without a profile |
| `merge-namespaces` | pruned | rules 1–5; checked build |
| `merge-namespaces-2` | pruned | rules 1–5; checked build |
| `merge-namespaces-nested` | pruned | rules 1–5; checked build |
| `merge-namespaces-nested-then-plain` | pruned | rules 1–5; checked build |
| `namespace-alias-dangling` | flat-with-reason | unsupported A6 dialect `c++2c` (C++26) |
| `no-optimize` | pruned | rules 1–5; checked build |
| `pull-headers-up` | flat-with-reason | non-standard system headers `myvector.h` / `mymap.h` |
| `qualifiers` | pruned | rules 1–5; checked build |
| `record-layout-special-members` | pruned | rules 1–5; checked build |
| `references-from-template-arguments` | pruned | rules 1–5; checked build |
| `remove-comments` | error-with-reason | GCC rejects clang-only `-fparse-all-comments` |
| `remove-namespaces` | pruned | rules 1–5; checked build |
| `remove-template-functions` | pruned | rules 1–5; checked build |
| `remove-type-alias` | pruned | rules 1–5; checked build |
| `repair-gives-up` | pruned | rules 1–5; checked build |
| `repair-restores-function` | pruned | rules 1–5; checked build |
| `repair-restores-macro` | pruned | rules 1–5; checked build |
| `repair-restores-overloads` | pruned | rules 1–5; checked build |
| `repair-restores-type` | pruned | rules 1–5; checked build |
| `sizeof` | pruned | rules 1–5; checked build |
| `source-ranges` | pruned | rules 1–5; checked build |
| `static-assert` | pruned | rules 1–5; checked build |
| `static-assert-branch` | pruned | rules 1–5; checked build |
| `std-namespace` | flat-with-reason | non-standard system header `mystd.h` |
| `stl` | raw-with-reason | unmodelled compiler `-v`; raw preprocessing fits 64 MiB |
| `strip-comments-all` | pruned | rules 1–5; checked build |
| `template-alias` | pruned | rules 1–5; checked build |
| `template-friend` | error-with-reason | baseline link: undefined `D<int>::f()` |
| `template-if-constexpr-member` | pruned | rules 1–5; checked build |
| `template-partial-spec-dependent` | pruned | rules 1–5; checked build |
| `template-variables` | pruned | rules 1–5; checked build |
| `templated-context` | pruned | rules 1–5; checked build |
| `track-parent-decls` | pruned | rules 1–5; checked build |
| `ull` | pruned | rules 1–5; checked build |
| `unused-fields` | pruned | rules 1–5; checked build |
| `using-base-grandparent` | pruned | rules 1–5; checked build |
| `using-base-member-template` | flat-with-reason | unsupported A6 dialect `c++2c` (C++26) |
| `using-declarations` | error-with-reason | baseline link: undefined `ns3::used3()` |
| `verify-fallback-judge-fails` | pruned | rules 1–5; checked build |
| `verify-fallback-judge-missing` | pruned | rules 1–5; checked build |
| `verify-run-detects-difference` | pruned | rules 1–5; checked build |
| `verify-run-passes` | pruned | rules 1–5; checked build |
