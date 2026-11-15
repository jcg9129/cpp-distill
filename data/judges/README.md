<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Judge profiles

`<name>.toml` uses the strict flat TOML subset in `src/core/README.md`. `name`
(lowercase letters, digits, hyphens) matches its filename. Required fields are
`name`, `description`, `compiler = { family = "gcc", version = "major.minor[.patch]" }`,
`triple`, `std`, `flags`, `libraries`, `check`, `updated` and nonempty HTTP(S)
`sources`. Optional fields are `link`, `inert`, `build`, `snapshot_compiler`,
`predefined`.
`defines`, `library` and `allowed` are obsolete and rejected.

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
python3 ci/profiles/snapshot.py data/judges/atcoder.toml --compiler /path/to/g++
# In the CI job that actually built the recipe, additionally affirm its identity:
python3 ci/profiles/snapshot.py data/judges/atcoder.toml --compiler /workspace/gcc/bin/g++ \
  --reproduced-build 'recipe=https://img.atcoder.jp/file/language-update/2025-10/017-23_gcc_14-2-0.toml'
```

The exact effective command is `-x c++ -std=<std> <flags>`. A separate `-dD -E`
run detects startup headers from line markers (not `-H`); only `stdc-predef.h`
is accepted. Its definitions are marked as header facts. All special registry
names are probed with `#ifdef` under that same argv. The x86 native model cites
GCC's target, common frontend and native-driver source for each audited release.
For native flags, the compiler's option diagnostic supplies every CPU spelling;
each ABI-compatible `-march`/`-mtune` combination replaces native **in place**,
retaining later options and macro operations. Only model names are joined:
absent everywhere → undefined; identical definition → known; always present but
differing → defined-unknown; otherwise → unknown. Any variation outside the model
fails with the names. With only `-mtune=native`, only `tune` names may vary.
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
The shipped `codeforces-predefined.txt` comes byte for byte from
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
its `atcoder-predefined.txt` is shipped byte for byte. This confirms the
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
