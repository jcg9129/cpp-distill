<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Judge profiles

`<name>.toml` uses the strict flat TOML subset in `src/core/README.md`. `name`
uses `[a-z0-9-]`, has shape `<judge>-gcc<major>-<cppNN|gnuppNN>[-<variant>]`,
and matches its filename. Judge and variant segments are nonempty lowercase
letters/digits separated by hyphens; the major is positive. `c++23` becomes
`cpp23`, and `gnu++23` becomes `gnupp23`. A variant suffix distinguishes options
that would otherwise collide. Full names pin a compiler option and never change
meaning. The shipped names are `codeforces-gcc14-cpp23` and
`atcoder-gcc15-gnupp23`, with optional `alias = "codeforces"` and
`alias = "atcoder"`, respectively. Aliases use nonempty `[a-z0-9-]+` strings and
track the newest GCC option; future alias moves belong in release notes. Required fields are
`name`, `description`, `compiler = { family = "gcc", version = "major.minor[.patch]" }`,
`triple`, `std`, `flags`, `libraries`, `check`, `updated` and nonempty HTTP(S)
`sources`. Optional fields are `link`, `inert`, `build`, `snapshot_compiler`,
`predefined`, `alias`, `probe_source_bytes`, `probe_output_bytes`.
`defines`, `library` and `allowed` are obsolete and rejected.

Selectors without a separator or `.toml` suffix resolve a full name or alias
across bundled and user profiles. The user directory follows `XDG_CONFIG_HOME`
(absolute paths only), `HOME/.config` or Windows `APPDATA`, under `distill/judges`.
All user TOML files are validated in sorted filename order. Invalid files and
colliding user identifiers are errors, including for `--judge list`. Two user
aliases cannot coincide, nor can a user's name equal another user's alias.
A user name (the TOML filename stem) or alias overrides the identical bundled
identifier and emits a warning on every catalog-based run. Shadowing is per
identifier: overriding an alias leaves the bundled full name available unless
that identifier is also overridden. The list includes each bundled and user
profile, including shadowed entries, distinguished by origin. Explicit TOML
paths load directly, without consulting the catalog.

Every profile selection prints `judge: <selector> -> <resolved name>` to stderr.
The report includes that line plus `judge selector`, `judge resolved name`,
`judge origin` (`bundled` or `user` with absolute TOML path), and `judge digest`.
The digest is lowercase SHA-256 of this byte sequence, in order:

1. The ASCII bytes `distill-profile-v1` followed by LF.
2. A frame containing the original TOML bytes.
3. If `predefined` is present, a frame containing its UTF-8 filename, then a
   frame containing the original snapshot bytes.

A frame is the ASCII decimal byte length (no leading zeroes), a colon, then
exactly those bytes; no separator or terminator follows a frame. Preserve BOM,
comments, whitespace and line endings. Hash the same bytes that were parsed,
without rereading files. The TOML's location and selector are excluded, so
moving a profile or selecting its alias preserves the digest. `predefined` is
the schema's explicit data-file reference. Embedded compiler registries, native
models, allow-lists and extension inventories belong to the distill version,
not this profile content digest. No snapshot is regenerated to obtain a digest.
The renamed shipped snapshots have no profile-name header; their contents are
unchanged from the CI artifacts cited below.

`flags` is an ordered argv, including `-D` and `-U` in place. It shares the
§4.1 command classifier and rejected options; response files, forced includes,
specs, plugins and forwarded preprocessor/linker/assembler options are forbidden.
`-std=` belongs only in `std`. Unmodelled options must be explicitly documented
in `inert`; this never excuses a rejected option. `link` contains linker-only
options, excluded from snapshots. `libraries` selects separate A1 sets: a macro
is protected only when absent from every selected set. A missing/empty selected
inventory grants no protection. Without a selection, the union of all sets applies.

`build` has exactly one key: `package` (exact package and revision) or `recipe`
(the published source/configure recipe URL). Reproduction relies on amended A4,
not version/triple similarity. `snapshot_compiler` has the same one-key form
and names the compiler used for an approximate snapshot when the judge build
is unknown or differs. Package values require `name=version` (including the
exact distribution revision and a distribution prefix where applicable); recipe values require an HTTP(S) URL.
For Codeforces, omit `build` and use:

```toml
# Judge publishes only "GNU G++23 14.2 (64 bit, msys2)"; package revision unknown.
snapshot_compiler = { package = "msys2:mingw-w64-x86_64-gcc=14.2.0-3" }
```

A profile is approximate if it has no build,
its snapshot is not from that build, or the compiler release lacks a complete
special-builtin registry. Approximate snapshots contribute only the commented
regex allow-list in `approximate-allowlist.txt`; other reserved facts remain
unknown. Explicit command operations remain established facts. Absent non-reserved
names are undefined under A5. Only reproduced profiles have A4's exhaustive
negative facts. Startup-header facts never acquire compiler-builtin provenance.
The allow-list includes `__has_include`: a positive special-builtin probe makes
it defined with an unknown definition; invoking it remains unknown.

`Profile::initialState(name)` returns undefined, known definition (including
function signature/replacement), defined with unknown definition, or unknown,
with provenance. `activeSpecialBuiltin()` identifies an active binding, so
`defined(__has_include)` can resolve while an invocation remains blocked.
`-U__has_include` removes it; `-D__has_include(x)=0` supplies an ordinary macro.
A3 checks the effective states, so a final `-U` cancels a prior definition.

## Snapshot format version 1

UTF-8, LF (CRLF accepted), one record per line. No arbitrary comment/directive
lines are accepted. The optional SPDX line is exactly the one below. Header
keys are required exactly once, except `snapshot-compiler`, required only when
the profile declares `snapshot_compiler`, and `flag`, repeated once per ordered argv
argument (zero repetitions for empty flags). Header values are nonempty strings;
`compiler-version` is the complete `--version` output with newlines spelled
`\n`. `gcc-version` is `-dumpfullversion -dumpversion`, `dumpmachine` the actual
`-dumpmachine`. The profile and compiler triples may differ only in the vendor
field (including omitted GNU Linux vendors); both spellings are recorded.

```text
# SPDX-License-Identifier: GPL-3.0-or-later
# distill-snapshot: 1
# compiler-path: /workspace/gcc/bin/g++
# compiler-version: g++ (GCC) 15.2.0\nCopyright ...
# gcc-version: 15.2.0
# dumpmachine: x86_64-pc-linux-gnu
# profile-triple: x86_64-linux-gnu
# build: recipe=https://example.org/install.toml
# reproduced: yes
# std: gnu++23
# builtin-registry: gcc-15.2
# native-model: none
# flag: -O2
# flag: -DONLINE_JUDGE
# end-header
#define __GNUC__ 15
# startup-header stdc-predef.h #define __STDC_IEC_559__ 1
# command-operation #define ONLINE_JUDGE 1
# state __has_include defined-unknown special-builtin
# probe __has_include defined
```

The fragment illustrates record syntax, not a complete valid snapshot. All
registered names need probes and corresponding facts. `build` is
`package=<value>`, `recipe=<url>` or `none`. When present, `snapshot-compiler`
must exactly match the profile's `snapshot_compiler` identity in the same form.
An approximate Codeforces snapshot has these header lines:

```text
# build: none
# snapshot-compiler: package=msys2:mingw-w64-x86_64-gcc=14.2.0-3
# reproduced: no
```

`reproduced` is `yes` or `no`; `yes` without profile `build` is an error, as is
`yes` with a different `snapshot_compiler`. An existing profile `build` alone
never affirms reproduction; only the CI job using that package/recipe may pass
`--reproduced-build`.

`builtin-registry` is the compiler's `gcc-M.m` key or `none` if unsupported.
`native-model` is `gcc-M.m-x86` when a supported host-dependent option is present,
otherwise `none`. The loader cross-checks the exact profile std/flags/version,
triple compatibility, registry, native coverage, command operations and probe
results. Missing, duplicate, contradictory or unknown records are errors.

Ordinary `#define NAME...` records have compiler-builtin provenance. Known
startup, command and native definitions use respectively
`# startup-header stdc-predef.h #define ...`,
`# command-operation #define ...`, and `# native-model #define ...`.
Definitions preserve function parameters, variadics and replacement text.
Other states use `# state NAME STATE ORIGIN`, where STATE is `undefined`,
`defined-unknown` or `unknown`. ORIGIN is `special-builtin`, `command-operation`
or `native-model` (command operations can only record undefined in this form).
`# probe NAME defined|undefined` records every registry probe. A known ordinary
definition takes precedence over a positive special probe. Negative probes
require an undefined fact. There is exactly one fact per name; probes are separate.
No host-dependent option means no native records or join.

Generate with Python 3.11+:

```sh
python3 ci/profiles/snapshot.py data/judges/atcoder-gcc15-gnupp23.toml --compiler /path/to/g++
# In the CI job that actually built the recipe, additionally affirm its identity:
python3 ci/profiles/snapshot.py data/judges/atcoder-gcc15-gnupp23.toml --compiler /workspace/gcc/bin/g++ \
  --reproduced-build 'recipe=https://img.atcoder.jp/file/language-update/2025-10/017-23_gcc_14-2-0.toml'
```

The exact effective command is `-x c++ -std=<std> <flags>`. A separate `-dD -E`
run detects startup headers from line markers (not `-H`); only `stdc-predef.h`
is accepted. Its definitions are marked as header facts. All special registry
names are probed with `#ifdef` under that same argv. The x86 native model cites
GCC's target, common frontend and native-driver source for each audited release.
For native flags, the compiler's option diagnostic supplies every CPU spelling;
each ABI-compatible `-march`/`-mtune` combination replaces native **in place**,
retaining later options and macro operations. Any variation outside the model
fails with the names. The sweep never supplies a state: every model name is
recorded as unknown, because `-march=native` takes ISA bits (`-mshstk`, `-mrtm`,
…) and cache parameters from CPUID, beyond any named preset. Only a model name
fixed by a command `-D`/`-U` is joined (absent everywhere → undefined; identical
definition → known; always present but differing → defined-unknown; otherwise →
unknown). With only `-mtune=native`, only `tune` names may vary.
The source audit bounds the dependency inventory; named-CPU sweeps are regression
checks, not a claim that named CPU presets exhaust physical feature masks.
The initial implementation supports the shipped x86_64 ABI for native profiles;
other host-dependent targets are rejected rather than sampled on this host.

## Research and CI

Codeforces publishes its [command and compiler label](https://codeforces.com/blog/entry/133046?mobile=false),
"GNU G++23 14.2 (64 bit, msys2)", but no MSYS2 package revision. PBOX's
bundle revision and a matching MSYS2 source tree do not establish the judge's
package identity. The profile therefore omits `build` and is **approximate**.
Its `snapshot_compiler` is the available GCC `14.2.0-3` MSYS2 package.
The shipped `codeforces-gcc14-cpp23-predefined.txt` comes byte for byte from
[`profiles.yml` run 43771140647](https://github.com/jcg9129/cpp-distill/actions/runs/43771140647).

`ci/profiles/matrix.py` selects `build` when present, otherwise
`snapshot_compiler`. The Windows installer derives the exact GCC filename from
that identity. `ci/profiles/mingw-packages.txt` pins the available 16-package
runtime dependency closure, not a claimed judge manifest. On 2026-11-15 all
listed files were downloaded from the [MSYS2 repository](https://repo.msys2.org/mingw/mingw64/)
and their `.PKGINFO` records inspected. This includes GCC and gcc-libs
`14.2.0-3`; `14.2.0-1` is unavailable. Some historical dependencies have also
been removed, so the lock uses available revisions satisfying the package's
requirements. This is another reason these artifacts cannot establish the
judge's build.

`ci/profiles/mingw.py` checks every exact filename against the live listing,
downloads it, verifies `.PKGINFO` name/version, and resolves all transitive
`depend` records against the locked packages and their `provides` aliases.
Version constraints include package revisions when specified; version bounds
use MSYS2's `vercmp`. Missing files, missing or ambiguous dependencies, version
mismatches and unused lock entries fail clearly before installation. Update the
lock by inspecting the compiler's `.PKGINFO` and recursively selecting available
files for its dependencies; never substitute a missing revision silently.
CI installs the validated closure in one `pacman -U` transaction, with no
rolling-package fallback, and verifies every installed revision. The Codeforces
snapshot records `snapshot-compiler` and `reproduced: no`; only an actual `build`
identity is passed as `--reproduced-build`.

AtCoder's [published install script](https://img.atcoder.jp/file/language-update/2025-10/017-23_gcc_14-2-0.toml)
now uses GCC **15.2.0**, despite its older URL name. Source URL:
`http://ftp.tsukuba.wide.ad.jp/software/gcc/releases/gcc-15.2.0/gcc-15.2.0.tar.gz`.
The compiler stanza is reproduced in `ci/profiles/atcoder-recipe.sh`, using
`../configure CC=gcc CXX=g++ --prefix=<workspace>/out/atcoder-gcc
--enable-languages=c++ --disable-bootstrap --disable-multilib
--disable-libsanitizer --disable-checking --disable-nls --disable-gcov
--disable-libada --disable-libgm2`. CI uses ubuntu-24.04, `make -j$(nproc)`,
a 240-minute limit, and a SHA-pinned actions/cache keyed by recipe hashes. A
hash check rejects upstream recipe drift. The reproduced build printed
`x86_64-pc-linux-gnu` for `-dumpmachine` in
[`profiles.yml` run 43768705680](https://github.com/jcg9129/cpp-distill/actions/runs/43768705680);
its `atcoder-gcc15-gnupp23-predefined.txt` is shipped byte for byte. This confirms the
reproduced compiler's triple; the profile retains its other UNVERIFIED notes.
The profile keeps the published preprocessing options in order, including modules,
OpenMP, pthread, optimization and native tuning. AtCoder's `-I<install>/include`
exposes ACL, Boost, etc. as judge system headers outside the standard-header list.

Both jobs upload snapshots for review; they never commit or publish changes.
The two shipped snapshots are validated by embedded-profile tests. The four
shipped A1 inventories in `data/extensions/` come byte for byte from the merged
artifacts of [`extensions.yml` run 43768709866](https://github.com/jcg9129/cpp-distill/actions/runs/43768709866);
their comments record each scan.
A named missing snapshot is always an error; it never downgrades silently.
User profiles may omit `predefined` to use explicit command facts only.
For updates, recheck sources and date, regenerate snapshots and reachable-header
A1 inventories, and review their diffs together. See design §4.6: validation must
record its actual argv and omissions, probe that exact configuration (ordinary
and special macros), and use consistent build options. Matching facts used by a
resolution is weaker than target verification; local native options never confer
target equivalence. Resolver/validation orchestration remains the next D2 work.

## Diagnostic judge probes (D6b)

The optional strict-schema TOML integer keys `probe_source_bytes` and
`probe_output_bytes` each accept 1024 through 16777216 inclusive. Both default
to **60000 bytes**. Codeforces and AtCoder currently use this conservative
fallback: no published source/output limit has been established here, and these
values are not claims about either judge's limit. An override must cite the
judge's published limit in a comment. Probe output has a conservative static
bound: stringized values are limited to 256 bytes (larger expansions produce an
explicit probe failure), hex encoded on output. The bound includes protocol
lines and every fact, including unsupported records, with allowance for Windows
CRLF output. Both budgets apply to each
part; increase M in `--part N/M` until every part fits. There are no empty parts.
Every part is required by the checker. Source generation is deterministic for
the profile digest and distill build version; use that same version to check.

`--judge-probe <selector>` emits presence captures before any helper macro or
header include. It checks all snapshot/registry names and fixed judge-style
names (`ONLINE_JUDGE`, `EVAL`, `LOCAL`, `_DEBUG`, `DEBUG`, `ATCODER`,
`CODEFORCES`, `NDEBUG`, plus `__clang__` for compiler diagnostics). It next
stringizes audited object-like definitions with guarded `__distill_probe_*`
helpers. Transitive contextual builtins, `_Pragma`, unbalanced parentheses,
recursive expansions and token construction (`#`/`##`) are excluded;
function-like macros are presence-only. The latter exclusions conservatively
narrow the supported subset, never broaden it. Finally it diagnoses all headers
in the embedded standard-header inventory, the representative PBDS headers
`ext/pb_ds/assoc_container.hpp` and `ext/pb_ds/tree_policy.hpp`, and explicit
library version headers:
libstdc++ `bits/c++config.h`, glibc `features.h`, MinGW `_mingw.h`, and macOS
`Availability.h`. Version keys include the header stage. These diagnostics do
not validate header contents or A1 lists. Builtin activity is untested.

Native joined `unknown` permits either presence state, `defined-unknown`
requires presence, and invariant known/undefined bindings compare normally.
Expansion comparisons propagate dependencies transitively through the snapshot;
a variable joined native state permits a variable expansion without narrowing
the model. No native name is simply ignored because it appears in the model.

The output protocol (format **1**) is LF-delimited ASCII; CRLF transport is
accepted and normalized for the checksum. It has:

- `DPROBE|1|<profile>|<digest>|<hex distill version>`
- `PART|<N>|<M>|<part fact-set SHA-256>|<profile digest>|<full fact-set SHA-256>`
- `<category>|<key>|<hex UTF-8 value>` for each fact. Categories are `presence`,
  `expansion`, `builtin`, `header`, `version`, `assumption`. Unsupported values
  begin `unsupported:`; runtime probe errors begin `!probe-failure:`.
- `END|<number of preceding lines>|<decimal FNV-1a-32 checksum>`

The checksum covers every preceding line including LF, starting at 2166136261,
XORing each byte then multiplying by 16777619 modulo 2^32. Fact-set hashes cover
ordered `category|key|unsupported-reason` records, each followed by LF (an empty
reason has an empty last field). Complete manifests may use a different partition
from the expected file, but may not mix partition totals or distill/profile
versions. Missing, duplicate, misplaced or mixed parts are rejected. These
checks detect accidents, not fabrication or mixing samples from machines whose
identity is unobservable. The checker reports each selected fact and classifies
failures as format error, wrong compiler or option selected, contextual field,
probe failure, fact mismatch, or contradicted assumption. Unsupported comparable
facts fail; intentionally presence-only/coverage records are skipped and counted.

`<name>-probe-expected.txt` is an optional sibling of the TOML/snapshot,
embedded by `cmake/EmbedProfiles.cmake`. It is **excluded from the v1 content
digest**: its own output carries that digest, so including it would be circular.
Observations are also excluded. The digest rule above remains unchanged, and
expected headers still bind to the TOML and snapshot bytes exactly. Missing
expectations are explicit failures: `no expected output shipped for <profile>;
run profiles.yml`. Expectations will be committed from a reviewed CI artifact;
none is fabricated from replacement lists. Fixture expectations only test the
protocol.

Immediately after each snapshot step, `profiles.yml` builds the LLVM-free
`DISTILL_PROFILE_ONLY` diagnostic CLI and runs `ci/profiles/probe.py` with that
job's snapshot compiler. This loads the freshly generated snapshot by explicit
TOML path, finds a partition satisfying both budgets for every part, compiles
and runs all probes, checks the complete result, and uploads the expected file
alongside the snapshot. It uses the snapshot's exact `-x c++ -std=<std> <flags>`
argv, retaining ordered macro operations and native options. Linker-only
`link` options are excluded as in snapshot capture: no judge-installed external
library is needed by this diagnostic. Both the AtCoder recipe build and the
Codeforces exact MinGW package job follow this path. Do not substitute a local
compiler's stringization for this output.

`--judge-check <selector> <output-file>... [--record <dir>]` never changes
`initialState()` or profile facts. Records are named
`<name>-<YYYY-MM-DD>-<n>.txt`, starting with the next free positive number, and
include selector, resolved profile, origin/path, digest, UTC date, scope,
checked/skipped counts, result and original output. The scope is "custom test of
that option, that date" and a pass is labelled **user-reported match**. A1/A4/A5
remain assumptions. Any shipped-profile fact mismatch or contradicted assumption
blocks the next release until investigated, with no automatic downgrade.

Stored observations in `data/judges/observations/*.txt` are **embedded**, not
read from a runtime checkout. `--judge list` shows the latest date and numeric
sequence whose record names this exact profile and digest, with its status;
otherwise it shows `-`. This works in relocated release binaries. Submit records
for review; they are evidence, not authenticated execution. Updating observations
or expectations does not change a profile digest. Rebuild to display newly
stored observations. User profiles use the same embedded observations only when
their name and digest match.
