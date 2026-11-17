<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Command-option soundness audit (2026-11-17)

**Verdict: no unsound command-option path found.** Several `harmless` options
affect predefined macros. The label is misleading, but the audited resolution,
satisfaction and target-equivalence paths do not assume macro invariance from
that label. Retaining incompatible local options can unnecessarily force `raw`;
this is **imprecise but sound**. Production code and `ci/profiles/flags.py` remain
unchanged. This conclusion assumes the documented profile provenance and A1–A5;
probing a local compiler alone cannot establish the judge's environment.

Inspected design §§4.1, 4.5, 4.6 and implementation. Experiments use
`g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0`, `aarch64-linux-gnu`, normally `gnu++20`.
Synthetic test profiles describe that actual compiler; no shipped x86 integer
model is used by the new tests.

In the table, **A** means no-profile resolution leaves external option-dependent
names unknown; source-derived definitions and the effective dialect still work.
**B** means profile resolution reads the snapshot taken under the profile's full
flags, then checks the mixed configuration by probing it and rerunning resolution.
**C** means `target` requires exhaustive macro equality, not just the macros used
by resolved conditions. These mechanisms apply to every row, regardless of class.

| Option / actual class | GCC macro effects and source evidence | (a) No profile | (b) Profile | (c) Target | Verdict |
|---|---|---|---|---|---|
| Accepted `-O0`/`-O1`/`-O2`/`-O3`/`-Os`/`-Og`, harmless | `__OPTIMIZE__`, `__OPTIMIZE_SIZE__` (`-Os`), `__NO_INLINE__` (`-O0`); [initial optimization macros][opt], [no-inline][misc]. | A | B | C | Imprecise but sound |
| `-Ofast`, harmless | Also `__FAST_MATH__`, `__FINITE_MATH_ONLY__`, `__NO_MATH_ERRNO__`, `__RECIPROCAL_MATH__`, `__NO_SIGNED_ZEROS__`, `__NO_TRAPPING_MATH__`, `__ASSOCIATIVE_MATH__`; [math macros][math], [implied fast-math][fast]. `__GCC_IEC_559` and `__GCC_IEC_559_COMPLEX` change; [IEEE calculation][iec]. AArch64 additionally exposes `__ARM_FP_FAST`; [target hook][arm]. | A | B | C | Imprecise but sound |
| `-pthread`, harmless | `_REENTRANT` on this target; [AArch64 Linux CPP_SPEC][pthread]. Target-dependent elsewhere. `-lpthread` is different. | A | B | C | Imprecise but sound |
| `-Wdeprecated` / `-Wno-deprecated`, harmless | `__DEPRECATED` follows `warn_deprecated`; [definition][deprecated]. `-Wno-deprecated-declarations` does not remove it. | A | B | C | Imprecise but sound |
| `-Wvla` / `-Wno-vla`, harmless | `__cpp_runtime_arrays`: present when `pedantic ? warn_vla == 0 : warn_vla <= 0`; [condition][features]. | A | B | C | Imprecise but sound |
| `-Werror=…`, harmless | Can enable its warning, so `-Werror=vla` removes `__cpp_runtime_arrays`; [warning implication][werror]. `-Werror=deprecated` alone has no dump delta because that warning is already enabled. No blanket invariance claim is valid. | A | B | C | Imprecise but sound |
| `-pedantic`, `-pedantic-errors`, `-Wpedantic` and its negative/error forms, harmless | `__cpp_binary_literals` in pre-C++14 modes, `__cpp_hex_float` when extended numbers are disabled, and `__cpp_runtime_arrays`; [feature conditions][features], [aliases][pedantic]. Explicit `-Wno-vla` restores the VLA macro even with pedantic mode. | A | B | C | Imprecise but sound |
| Accepted `-g`, `-g0`…`-g3`, harmless | No dump changes on this C++ Linux configuration. This is not universal: [CFI macro][cfi-macro] depends on [frame/unwind/debug settings][cfi]. Other `-g*` spellings are unmodelled. | A | B | C | Sound here; other target effects still probed |
| Accepted `-static`, `-static-libgcc`, `-static-libstdc++`, `-s`, `-pipe`, `-lm`, `-lpthread`, `-lstdc++`, harmless | No dump changes here; [link spec][link], [driver handling][driver], [C++ library driver][cxx-link]. Linux's inspected CPP_SPEC has no static/link test. Other `-static*` and arbitrary libraries are not all harmless. | A | B; check compilers replace local link settings | C | Sound |
| `-w`, diagnostic color / max-errors options, harmless | No predefined-macro change found; `-w` suppresses diagnostics without resetting the feature-warning switches above. | A | B | C | Sound |
| `-fsanitize=…`, also harmless | E.g. `__SANITIZE_ADDRESS__`, `__SANITIZE_THREAD__`, `__SANITIZE_HWADDRESS__`; [definitions][sanitize]. Included because the classifier accepts it too. | A | B | C | Imprecise but sound |
| `-fno-exceptions`, `-fno-rtti`, modelled | `__EXCEPTIONS` / `__cpp_exceptions`, `__GXX_RTTI` / `__cpp_rtti`; [exceptions][exceptions], [RTTI][deprecated]. | A | B | C | Imprecise but sound |
| Other modelled machine/type/module/OpenMP options | ABI/type/ISA macros, `__CHAR_UNSIGNED__`, wchar macros, `__cpp_modules`, `_OPENMP`; [type/target hooks][types], [modules][modules], [character/OpenMP definitions][misc]. Actual `-fopenmp` also defines `_REENTRANT` here. | A; no inferred local integer model | B, including snapshot integer requirements | C | Imprecise but sound |
| `-Oz`, unmodelled | GCC defines the size/optimization macros, but neither classifier's accepted set includes this spelling. | Resolution disabled | Resolution disabled | Still compares actual probes | Sound, conservative |

`-Ofast` also changes startup-header IEC macros in this environment. Ubuntu's
driver supplies `_FORTIFY_SOURCE=3` at nonzero optimization. Complete effective
dumps capture both effects; testing just the initially suspected names would
miss them. The inert-row regression compares whole dumps.

The VLA test is unchanged in [GCC 13.4 lines 959–965][vla13],
[14.3 lines 959–965][vla14], [15.2 lines 962–968][features], and
[GCC 16 lines 981–987][vla16]. The GCC 16 citation pins the inspected
`releases/gcc-16` commit, rather than a moving branch.

The classifier is `src/core/command/command.cpp:125–149`, mirrored by
`ci/profiles/flags.py`. **Modelled does not mean the resolver synthesizes
predefined macros from that option.** Both modelled and harmless flags survive
configuration construction and probes. Modelled flags additionally guide the
Clang parse translation (`src/core/prune/extract.cpp:44–98`); optimization and
`-pthread` have explicit translations too, and `-Ofast` disables pruning when
its driver semantics cannot be translated. Unknown user conditions and differing
predefined expansions also disable pruning (`src/core/prune/prune.cpp:150–173`).
This audit runs LLVM-free resolution/validation tests, not a new Clang audit.

For **(a)**, `src/core/pipeline/pipeline.cpp:259–262` passes only the effective
dialect when there is no profile. Local macro dumps never become judge facts.
Reserved option macros therefore stay unknown. A local `-fno-rtti`, for example,
does not make the resolver assume that the judge lacks RTTI.

For **(b)**, `ci/profiles/snapshot.py:189–205` captures the full profile argv.
`Profile::initialState` (`src/core/profile/facts.cpp:63–83`) uses its effective
facts; approximate snapshots withhold most optimization/build macros, but may
still use allowed `__cpp_*` feature facts. `flatCommand`
(`src/core/validate/configuration.cpp:46–64`) keeps other local compile flags,
removes local macro operations/dialect/user include paths, and appends profile
dialect and flags. This applies to check compilers too, subject to their separate
link-option replacement. Option ordering is left to the actual compiler.

Concrete outcomes below assume an exact profile of the local compiler. A dash
means an existing profile with default compile flags, not absence of a profile.
“Fallback” means the judge-directed decision remains valid, but no mixed checking
configuration satisfies it, so the pipeline delivers conditional `raw` bytes.

| Local flags | Profile flags | Tested condition | Correct and observed result |
|---|---|---|---|
| `-O0` | `-O2` | `defined(__OPTIMIZE__)` | True; profile wins, checked `flat`, target |
| `-O0` | — | `defined(__NO_INLINE__)` | True; checked `flat`, target |
| `-O2` / `-Os` | — | optimization / size definedness | False; fallback, local |
| `-O2` | `-O0` | `defined(__NO_INLINE__)` | True; appended level wins |
| — | `-Os` | `defined(__OPTIMIZE_SIZE__)` | True; checked configuration matches |
| `-pthread` | — | `defined(_REENTRANT)` | False; fallback, local |
| — | `-pthread` | `defined(_REENTRANT)` | True; checked `flat`, target |
| `-Wno-deprecated` | — | `defined(__DEPRECATED)` | True; fallback, local |
| — | `-Wno-deprecated` | same | False; checked `flat`, target |
| `-Wno-deprecated` | `-Wdeprecated` | same | True; appended warning setting wins |
| `-Wvla`, `-Werror=vla`, or pedantic forms | — | `defined(__cpp_runtime_arrays)` | True; fallback, local |
| `-Wno-vla` | `-Wvla` | same | False; appended warning setting wins |
| `-pedantic -Wno-vla` | — | same | True; condition satisfies, other macros still govern target |
| `-Ofast` | — | `#if __FAST_MATH__` | False; fallback, local |
| — | `-Ofast` | same | True; checked `flat`, target (no pruning requested) |
| `-Ofast` | `-O2` | `#if __FINITE_MATH_ONLY__` | False; checked `flat`, target: GCC resets the implied math defaults using the final optimization level |
| `-pthread` | — | unrelated `defined(__DEPRECATED)` | True; checked `flat`, **local**, since the unused `_REENTRANT` still differs |

Older-dialect pedantic tests also check binary-literal and strict-mode hex-float
feature facts. Modelled exceptions/RTTI and sanitizer cases exercise the same
mismatch rejection. The three explicitly listed optimization feature names in
`src/core/resolve/resolve.cpp:96` are **not initially unknown with a complete
profile**: only a subsequent GCC options pragma invalidates them (line 276).
The regression asserts a known decision before a pragma and unknown afterwards.
That mechanism does not handle command-option differences; configuration probing
does.

For **(3)**, `probeCompiler` (`src/core/probe/probe.cpp:145–156,187–222`) forwards
all non-input/non-controlled arguments into `-dM`, special-builtin and startup
probes. It never drops harmless flags. `probeConfiguration` records those actual
observations as facts, and `satisfies` reruns the resolver with them
(`src/core/validate/configuration.cpp:12–38,67–75`). `sameDecisions`
(`src/core/resolve/resolve.cpp:400–408`) requires each decided direction again,
or proves it unreachable; unknown does not satisfy a decided condition. The
pipeline rejects non-satisfying configurations before preprocessing/building
`flat` (lines 287–305), and falls back to `raw` if none satisfies. The subsequent
token/location preprocess comparison is another check, not proof of an
unobserved judge environment. It would not repair an incorrect profile snapshot.

For **(c)/(4)**, `targetEquivalent`
(`src/core/validate/configuration.cpp:77–99`) compares **all** ordinary macros,
every registered special probe, identity/triple/dialect, ordered profile flags,
and host-dependent options. Thus harmless flags that leave any effective macro
difference deny `target`, even if every used condition satisfies. A local flag
overridden by appended profile flags may correctly retain `target`. Raw's local
configuration undergoes the same comparison.

Regression coverage is in `tests/unit/option_model_test.cpp`: synthetic exhaustive
target comparisons; exact flat argv for fallback/check compilers; real GCC macro
deltas and inert flags; no-profile unknowns; profile decisions and satisfaction
for both compiler paths; approximate profiles; older dialects; pragma invalidation; and pipeline
fallback/success. Pipeline tests independently preprocess delivered bytes against
the original under **only judge flags**, comparing tokens and logical locations.

Proposed design wording only: describe `harmless` as “accepted and forwarded;
may affect preprocessing; configuration probes determine its effective macros.”
Neither `docs/design.md` nor `docs/progress.md` was edited.

Verification: LLVM-free Debug build of `distill_unit_tests` and
`distill_resolve_cases`, using `timeout 1200 cmake --build build-option-audit
--target distill_unit_tests distill_resolve_cases -j2`. Relevant unit filters
`option_model_` (9), `command_` (18), `resolve_` (17), and `validate_` (10) all
passed without skips: **54 unit tests**. `distill_resolve_cases --filter pragma`
passed **2 cases**, including two token/location and two runtime comparisons.
The nine new tests include 26 profile/local flag combinations and 13 pipeline
scenarios. Temporary files, downloaded research sources and the build directory
were removed after verification. No commit was made.

[opt]: https://github.com/gcc-mirror/gcc/blob/releases/gcc-15.2.0/gcc/cppbuiltin.cc#L102-L105
[math]: https://github.com/gcc-mirror/gcc/blob/releases/gcc-15.2.0/gcc/cppbuiltin.cc#L107-L126
[fast]: https://github.com/gcc-mirror/gcc/blob/releases/gcc-15.2.0/gcc/opts.cc#L712-L713
[iec]: https://github.com/gcc-mirror/gcc/blob/releases/gcc-15.2.0/gcc/c-family/c-cppbuiltin.cc#L812-L902
[arm]: https://github.com/gcc-mirror/gcc/blob/releases/gcc-15.2.0/gcc/config/aarch64/aarch64-c.cc#L132
[pthread]: https://github.com/gcc-mirror/gcc/blob/releases/gcc-15.2.0/gcc/config/aarch64/aarch64-linux.h#L39
[deprecated]: https://github.com/gcc-mirror/gcc/blob/releases/gcc-15.2.0/gcc/c-family/c-cppbuiltin.cc#L938-L945
[features]: https://github.com/gcc-mirror/gcc/blob/releases/gcc-15.2.0/gcc/c-family/c-cppbuiltin.cc#L952-L968
[werror]: https://github.com/gcc-mirror/gcc/blob/releases/gcc-15.2.0/gcc/opts-common.cc#L1958-L1965
[pedantic]: https://github.com/gcc-mirror/gcc/blob/releases/gcc-15.2.0/gcc/c-family/c.opt#L2535-L2536
[cfi-macro]: https://github.com/gcc-mirror/gcc/blob/releases/gcc-15.2.0/gcc/c-family/c-cppbuiltin.cc#L1609-L1612
[cfi]: https://github.com/gcc-mirror/gcc/blob/releases/gcc-15.2.0/gcc/dwarf2cfi.cc#L3704-L3766
[link]: https://github.com/gcc-mirror/gcc/blob/releases/gcc-15.2.0/gcc/gcc.cc#L1159-L1180
[driver]: https://github.com/gcc-mirror/gcc/blob/releases/gcc-15.2.0/gcc/gcc.cc#L4650-L4678
[cxx-link]: https://github.com/gcc-mirror/gcc/blob/releases/gcc-15.2.0/gcc/cp/g++spec.cc#L178-L244
[sanitize]: https://github.com/gcc-mirror/gcc/blob/releases/gcc-15.2.0/gcc/cppbuiltin.cc#L93-L100
[exceptions]: https://github.com/gcc-mirror/gcc/blob/releases/gcc-15.2.0/gcc/c-family/c-cppbuiltin.cc#L1141-L1146
[types]: https://github.com/gcc-mirror/gcc/blob/releases/gcc-15.2.0/gcc/cppbuiltin.cc#L146-L158
[modules]: https://github.com/gcc-mirror/gcc/blob/releases/gcc-15.2.0/gcc/c-family/c-cppbuiltin.cc#L1106-L1110
[misc]: https://github.com/gcc-mirror/gcc/blob/releases/gcc-15.2.0/gcc/c-family/c-cppbuiltin.cc#L1590-L1660
[vla13]: https://github.com/gcc-mirror/gcc/blob/releases/gcc-13.4.0/gcc/c-family/c-cppbuiltin.cc#L959-L965
[vla14]: https://github.com/gcc-mirror/gcc/blob/releases/gcc-14.3.0/gcc/c-family/c-cppbuiltin.cc#L959-L965
[vla16]: https://github.com/gcc-mirror/gcc/blob/d877055e73de7f41a5c933b7eb2ebeaac87410b9/gcc/c-family/c-cppbuiltin.cc#L981-L987
