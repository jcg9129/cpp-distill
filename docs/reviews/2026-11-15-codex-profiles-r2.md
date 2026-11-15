<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Codex review P2: judge profile amendment (2026-11-15)

Question: challenge the proposed amendment to design §4.3 (judge flags, host-dependent options, build configuration, special builtins), revision v2, after the user chose A5. Answer verbatim:

**V2 is substantially better, but I would still revise it before approval.** It fixes ordered `-D`/`-U`, distinguishes builtin availability from invocation, rejects problematic command options, and improves validation probing. It only partly fixes inventory completeness and native dependencies.

I accept A5 as stipulated. I interpret revised A4 as replacing the old blanket accuracy assumption; otherwise A4 simply assumes away every disagreement. I checked GCC source and documentation, but did not execute judge binaries.

1. **Approximate profiles still assert unjustified positive facts.**  
   “Names in the snapshot: as recorded” applies equally to approximate profiles. A nearby compiler configured with an AVX2 default can supply `__AVX2__=1` while the judge’s same-version, same-triple compiler does not. Thus `#ifdef __AVX2__` incorrectly resolves true, without any `native` option. GCC supports such configured architecture defaults. Approximate profiles need individually justified facts for **presence and values**, not just conservative treatment of absent names. [GCC configuration](https://gcc.gnu.org/install/configure.html)

2. **The reproduced-build exception is conditionally sound, but its eligibility test is insufficient.**  
   It works if A4 explicitly establishes equivalence of the effective preprocessing environment, with unresolved host effects conservatively covered. A configure line alone does not establish that: source patches, detected build dependencies, installed specs and startup headers also matter.

   Additionally, an unsupported builtin registry must disqualify inventory completeness. Making existing *candidate names* unknown does not cover a new special builtin absent from both the candidate set and ordinary dump; the reproduced-build fallback would incorrectly make it undefined. Require a complete registry for the actual supported build/mode before granting exhaustive negative facts.

3. **The `-H` preinclude check has a concrete hole.**  
   GCC 15.2 enables include tracing **after** processing startup includes. Its glibc hook requests `stdc-predef.h`, so an empty `-H` trace does not establish that no header was processed. Header macros can consequently enter the snapshot with incorrect provenance. Use a separate preprocessing trace with line markers or equivalent verified instrumentation. Enforcing the prohibition correctly will also reject ordinary hosted GCC/glibc configurations; supporting them requires capturing the preinclude and its provenance. [Startup sequencing](https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-15.2.0/gcc/c-family/c-opts.cc), [glibc preinclude hook](https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-15.2.0/gcc/config/glibc-c.cc)

4. **The native audit must extend beyond `i386-c.cc`.**  
   `__FP_FAST_FMA` is generated in common frontend code from backend capabilities. A baseline without FMA and a native FMA-capable judge can disagree on its definedness. A list limited to the cited x86 file misses it. Likewise, tuning-dependent `__GCC_DESTRUCTIVE_SIZE` and `__GCC_CONSTRUCTIVE_SIZE` are defined in common code. “Only tuning macros” must include such derived facts, not merely `__tune_*`. [Common macro generation](https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-15.2.0/gcc/c-family/c-cppbuiltin.cc), [interference-size dependencies](https://gcc.gnu.org/onlinedocs/gcc-15.2.0/gcc/Optimize-Options.html)

   The state rule is also unnecessarily lossy: it omits **identical definition everywhere** and **absent everywhere**. For example, `-march=native -mno-avx2` can establish that `__AVX2__` is undefined; `-march=native -D__AVX2__=7` establishes its replacement. Join states across configurations *after* effective options and macro operations.

5. **Registry membership must not override actual macro state.**  
   With `-U__has_include`, the registry’s unconditional “defined, definition unknown” is wrong. With `-D__has_include(x)=0`, treating its invocation as an unknown builtin loses a known ordinary definition. Use probe results and effective definitions; a failed positive probe must either record absence or reject the profile. The invocation blocker should follow the active builtin binding, not its spelling. [GCC macro-option semantics](https://gcc.gnu.org/onlinedocs/gcc/Preprocessor-Options.html)

6. **Command construction and validation still have integration gaps.**  
   The written command `-std=<std> <flags>` lets a later `-std=` in `flags` defeat the existing profile-override rule, changing `__cplusplus`. Normalize the effective dialect explicitly. Replace §4.6’s remaining `defines`-replacement language with the ordered command model. Finally, retaining every option is insufficient for `target`: local `-march=native` still describes the local host. Require affirmative environment equivalence, including host scope; matching the decisions used remains a weaker check.

I would approve after those corrections. If “reproduced build” already means complete preprocessing-environment equivalence under A4, I accept the reserved-name exception in principle—but the probing, dependency coverage and state-precedence defects still need fixing.