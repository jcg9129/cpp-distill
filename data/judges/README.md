<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Judge profiles

`<name>.toml` uses the strict flat TOML subset in `src/core/README.md`. `name`
(lowercase letters, digits, hyphens) matches its filename. Required fields are
`name`, `description`, `compiler = { family = "gcc", version = "major.minor[.patch]" }`,
`triple`, `std`, `flags`, `libraries`, `check`, `updated` and nonempty HTTP(S)
`sources`. Optional fields are `link`, `inert`, `build`, `predefined`.
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
not version/triple similarity. A profile is approximate if it has no build,
its snapshot is not from that build, or the compiler release lacks a complete
special-builtin registry. Approximate snapshots contribute only the commented
regex allow-list in `approximate-allowlist.txt`; other reserved facts remain
unknown. Explicit command operations remain established facts. Absent non-reserved
names are undefined under A5. Only reproduced profiles have A4's exhaustive
negative facts. Startup-header facts never acquire compiler-builtin provenance.

`Profile::initialState(name)` returns undefined, known definition (including
function signature/replacement), defined with unknown definition, or unknown,
with provenance. `activeSpecialBuiltin()` identifies an active binding, so
`defined(__has_include)` can resolve while an invocation remains blocked.
`-U__has_include` removes it; `-D__has_include(x)=0` supplies an ordinary macro.
A3 checks the effective states, so a final `-U` cancels a prior definition.

## Snapshot format version 1

UTF-8, LF (CRLF accepted), one record per line. No arbitrary comment/directive
lines are accepted. The optional SPDX line is exactly the one below. Header
keys are required exactly once, except `flag`, repeated once per ordered argv
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
`package=<value>`, `recipe=<url>` or `none`. `reproduced` is `yes` or `no`;
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

Codeforces publishes its [command and MSYS2/PBOX selection](https://codeforces.com/blog/entry/133046?mobile=false).
[PBOX's catalog](https://pbox.me/packages) names `gcc14-64-msys2` version
`14.2.0-1`, archive `gcc14-64-msys2-14.2.0-1.7z`. The matching
[MSYS2 source tree](https://github.com/msys2/MINGW-packages/tree/f2010a2af323b9fcddb238a62dd19d36eb547aa8)
has GCC `pkgver=14.2.0`, `pkgrel=1`, and requires `gcc-libs=14.2.0-1`.
`ci/profiles/mingw-packages.txt` pins that dependency closure's package filenames
from the same source tree; it is best source evidence, not a byte manifest of
PBOX's bundle. CI installs the exact set in one `pacman -U` transaction from
`https://repo.msys2.org/mingw/mingw64/`, with no rolling-package fallback.
**Open:** on 2026-11-15 HEAD requests for GCC and gcc-libs `14.2.0-1` returned
404 there. Until those artifacts are available the Windows job fails explicitly.
No compiler package was downloaded on the shared VM. Review the artifact/build
identity before checking a snapshot in; neither PBOX evidence nor a version label
alone confirms every byte of the judge environment.

AtCoder's [published install script](https://img.atcoder.jp/file/language-update/2025-10/017-23_gcc_14-2-0.toml)
now uses GCC **15.2.0**, despite its older URL name. Source URL:
`http://ftp.tsukuba.wide.ad.jp/software/gcc/releases/gcc-15.2.0/gcc-15.2.0.tar.gz`.
The compiler stanza is reproduced in `ci/profiles/atcoder-recipe.sh`, using
`../configure CC=gcc CXX=g++ --prefix=<workspace>/out/atcoder-gcc
--enable-languages=c++ --disable-bootstrap --disable-multilib
--disable-libsanitizer --disable-checking --disable-nls --disable-gcov
--disable-libada --disable-libgm2`. CI uses ubuntu-24.04, `make -j$(nproc)`,
a 240-minute limit, and a SHA-pinned actions/cache keyed by recipe hashes. A
hash check rejects upstream recipe drift. The inferred default triple
`x86_64-pc-linux-gnu` remains **UNVERIFIED** until workflow output confirms it.
The profile keeps the published preprocessing options in order, including modules,
OpenMP, pthread, optimization and native tuning. AtCoder's `-I<install>/include`
exposes ACL, Boost, etc. as judge system headers outside the standard-header list.

Both jobs upload snapshots for review; they never commit or publish changes.
The shipped judge snapshots intentionally remain absent until these CI artifacts
are reviewed. A named missing snapshot is always an error; it never downgrades
silently. User profiles may omit `predefined` to use explicit command facts only.
For updates, recheck sources and date, regenerate snapshots and reachable-header
A1 inventories, and review their diffs together. See design §4.6: validation must
record its actual argv and omissions, probe that exact configuration (ordinary
and special macros), and use consistent build options. Matching facts used by a
resolution is weaker than target verification; local native options never confer
target equivalence. Resolver/validation orchestration remains the next D2 work.
