<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Codex review P1: judge profile amendment (2026-11-15)

Question: challenge the proposed amendment to design §4.3 (judge flags, host-dependent options, build configuration, special builtins). Answer verbatim:

I would not approve the amendment as written. Recording judge flags is necessary, but the proposed uncertainty lists do not justify retaining the rule **“everything else is undefined.”** There are also concrete omissions in the builtin list.

I assume the published commands in the question are accurate, and that profiles may cover compiler versions beyond the two shipped defaults. I checked GCC documentation and source; I did not execute the judge binaries. Some findings below are definite counterexamples, while the CPU-enumeration finding is a gap in the completeness argument.

**1. The dynamic-builtin list is already incomplete for GCC 15.2.**

GCC 15.2 registers `__has_embed`, `__has_extension`, `__has_feature`, and `__has_c_attribute` in addition to the proposed names. They are special builtins, not ordinary replacement-list macros. [GCC 15.2 builtin registration](https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-15.2.0/libcpp/init.cc)

For example:

```cpp
#ifdef __has_extension
int main() { return 0; }
#else
int main() { return 1; }
#endif
```

Under the amendment’s inventory rule, an omitted `__has_extension` becomes *undefined*. On GCC 15.2 it is defined. Unlike `__has_feature`, this spelling is also absent from §4.5’s unconditional blocker list.

**Fix:** maintain a compiler-family/version/mode-specific registry of special preprocessor facilities. Generate or audit it against compiler source, and probe each candidate with `defined`, under the effective command. Use that same registry in snapshot generation, resolution, and validation.

Adding four more names fixes today’s omission; it does not establish future exhaustiveness. An unsupported compiler release must not automatically inherit a complete inventory.

**2. “Defined in every profile and without a profile” is false.**

Builtin availability and builtin expansion are separate facts. For example, GCC 11’s special-builtin table lacks `__FILE_NAME__`; GCC 15’s includes it. Thus a profile-free `#ifdef __FILE_NAME__` cannot safely become true merely because distill knows that spelling. [GCC 11 builtin registration](https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-11.5.0/libcpp/init.c), [GCC 15 builtin registration](https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-15.2.0/libcpp/init.cc)

The correct initial states are:

| Evidence | State |
|---|---|
| Builtin availability established; expansion not modelled | Defined, definition unknown |
| Availability not established | Unknown |
| Absence established for the effective configuration | Undefined |

Without a profile, only guarantees of the supported language/compiler contract justify known definedness. Extension builtins generally remain unknown.

There is also needless loss: §4.5 currently makes a condition unknown whenever certain query names *appear*. Once availability is established, `defined(__has_builtin)` can resolve even though `__has_builtin(foo)` remains unknown. Apply the blocker to an actual builtin invocation, including one reached through expansion, rather than to every occurrence of its name.

For `__LINE__`, an optional later improvement is a contextual evaluator using the original invocation location. Otherwise keeping its value unknown is safe.

**3. Enumerating named CPUs does not establish the complete effects of `native`.**

GCC’s x86 native detection does more than choose a named `-march`: it emits individual feature-enabling and feature-disabling options based on the detected CPU. Named CPU presets therefore are not an exhaustive enumeration of native feature configurations. Virtualized or feature-masked machines are particularly relevant. [GCC 15.2 native CPU detection](https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-15.2.0/gcc/config/i386/driver-i386.cc)

Consequently, “unchanged across every named CPU” is not itself proof that a macro is invariant under `native`. Nor does it prove that an unobserved macro cannot appear. I have not established a particular GCC 15.2 macro that escapes the proposed sweep; the defect is that the sweep lacks the completeness property claimed for it.

**Fix:** use a versioned, audited overapproximation of every macro affected by native detection, covering individual ISA options, tuning, and macros derived from those settings. Sweeps are useful regression checks for this model, not its proof.

The scope must also follow the actual unresolved option:

- With only `-mtune=native`, do not automatically invalidate ISA facts established by a fixed `-march`.
- Preserve facts fixed by explicit overrides when the compiler’s option semantics establish them.
- Restrict alternatives to the actual target ABI and mode.
- If a macro is present in every allowed configuration but has varying definitions, use *defined, definition unknown*, not fully *unknown*.

That last distinction allows `#ifdef` to resolve without pretending to know a value.

**4. A finite build-difference blacklist cannot support an exhaustive inventory.**

Two builds of the same GCC version and triple can have different architecture defaults. GCC explicitly supports configure defaults for `-march`, `-mtune`, ABI and other target options. A compiler configured with an AVX2-capable default can define `__AVX2__` where another build does not—even when the published command contains no `native` flag, so rule 2 never runs. GCC also supports additional driver specs through configuration. [GCC configuration options](https://gcc.gnu.org/install/configure.html)

More fundamentally, specs or downstream patches can introduce a macro absent from every build in your comparison set. For example, a judge build that injects `-D__JUDGE_BUILD_VARIANT__=1` defeats the “absent from snapshot and blacklist ⇒ undefined” rule.

Comparing Debian, Docker, Homebrew and MSYS2 builds can discover differences. It cannot prove the absence of other differences.

**Fix:** distinguish two profile modes:

- **Complete:** the effective judge environment is captured, or its equivalence to the captured environment is explicitly established under a bounded, documented assumption.
- **Partial:** individual positive and negative facts are justified; unlisted names remain unknown, subject to the existing A3 rule.

A4 should identify the compiler build, effective options, builtin inventory and supported host scope to which completeness applies. Otherwise A4 merely assumes the conclusion the snapshot procedure was supposed to establish.

This blacklist is also needlessly lossy when applied universally. If a profile establishes `-D_FORTIFY_SOURCE=2`, that definition should remain known at source entry. If an exact configuration establishes PIC/PIE settings, a generic blacklist should not discard them.

**5. `defines` cannot represent the semantics of `-D` and `-U` as currently described.**

These sequences differ:

```text
-DMODE=1 -UMODE
-UMODE -DMODE=1
```

The first leaves `MODE` undefined; the second defines it. Likewise, `-U__STRICT_ANSI__` can remove a definition present in the snapshot. A set of definitions cannot express these operations. GCC processes `-D` and `-U` in order. [GCC preprocessor options](https://gcc.gnu.org/onlinedocs/gcc-9.5.0/gcc/Preprocessor-Options.html)

Preserve these distinctions too:

```text
-DNAME        # replacement: 1
-DNAME=       # empty replacement
-DNAME=0      # replacement: 0
```

The supplied `-DOR_PROTO_DLL=` makes this immediately relevant.

**Fix:** store ordered command arguments, or an ordered macro-operation sequence with full definitions and undefinitions. Produce the effective snapshot with those operations included, rather than relying on an unspecified “snapshot plus defines” merge.

Do not assume all options are independent of explicit definitions. For example, GCC’s `-fhardened` conditionally supplies definitions and other options, and its fortification setting depends on the system library. [GCC hardening options](https://gcc.gnu.org/onlinedocs/gcc-15.1.0/gcc/Instrumentation-Options.html)

**6. An empty-file dump is not necessarily a dump of compiler-origin macros only.**

The proposed filter retains options such as `-include` and `-imacros`, while dropping paths needed to interpret them. Consider:

```text
-Ijudge-config -include settings.h
```

If `settings.h` defines `JUDGE_MODE`, dropping `-I` can select a different header or make the probe fail. Retaining the header’s resulting macros without their origin creates another problem: §4.5 might grant them the preservation promised specifically for compiler-predefined macros.

GCC also has standard preinclude machinery, so even a source file with no explicit includes need not imply that no header was processed. [GCC option and preinclude handling](https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-15.2.0/gcc/c-family/c-opts.cc)

**Fix:** define the snapshot boundary as “state immediately before the first user-source token,” and retain provenance:

- compiler builtin;
- driver/command definition;
- implicit or forced-header definition.

Then apply the corresponding header-transition rules. Being present in a dump must not itself confer A2(a) protection.

If §4.1 already rejects forced includes, specs, plugins or similar mechanisms, explicitly apply those restrictions to profile commands too. Do not silently accept them through `flags`. Parse options structurally; for example, `-Wp,...` is preprocessor forwarding, not a warning option.

Library configuration macros also need this separation. `_GLIBCXX_*` and `__GLIBCXX__` are not one interchangeable compiler-builtin family. Facts about ordinary library headers belong to the header model unless those headers were actually processed before source entry.

**7. Validation must probe the configuration actually used, including special builtins.**

“Use the flags where the local compiler accepts them” is acceptable only as construction of a distinct validation configuration. It is not evidence that the resulting compiler represents the judge.

For example, dropping unsupported `-fmodules` can change `__cpp_modules`. Retaining a local `-fno-exceptions` can change `__EXCEPTIONS__`. Applying `-march=native` locally describes the local build host.

**Fix §4.6 explicitly:**

- Construct and record the actual validation argument vector, including option ordering and any omissions.
- Probe that exact configuration with both the ordinary macro dump and special-builtin definedness probes.
- Use its real external facts for the resolver rerun.
- Require probes, preprocessing checks and builds to use consistent semantic options.
- Preserve the existing distinction between satisfying the decisions used and matching the judge environment.

A dropped option can still leave a configuration adequate to check particular resolutions. It cannot silently become target-equivalent.

The current `target` label also needs revision: matching triple and version is insufficient after the amendment explicitly acknowledges build-dependent defaults. Report build provenance and unresolved host scope separately.

**8. Vendor-field compatibility is a useful admission rule, not an equivalence proof.**

Accepting `x86_64-pc-linux-gnu` and `x86_64-linux-gnu` as compatible target descriptions is reasonable for this case. It does not establish matching compiler defaults, patches, sysroots, preincluded headers or libraries. Even identical triples would not establish those properties.

An `UNVERIFIED` triple must have an operational consequence: facts depending on its unknown components stay unknown. Independently established facts—such as x86-64 architecture—need not be discarded.

I would replace the amendment’s central rule with:

> A profile describes a justified set of possible effective judge environments. A macro is known undefined only if absent in every environment; known with a definition only if that definition is identical in every environment; and defined with unknown definition if present in every environment but its definition is unresolved. Otherwise it is unknown. Unlisted names default to undefined only when inventory completeness is established for the entire set.

This does not require implementing a general configuration solver. An exact snapshot mode and a conservative partial-facts mode are sufficient initially.

I would change my assessment if the profiles were tied to verified judge compiler artifacts and startup environments, special builtins were inventoried per supported version, and native-dependent facts were captured on all relevant build hosts or covered by an audited dependency model. Under those conditions, most blanket uncertainty lists could shrink substantially. Without them, the safe amendment is to weaken the completeness claim, not just enlarge the blacklist.