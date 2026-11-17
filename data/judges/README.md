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
track the newest supported GCC option. D6c adds `cses-gcc13-cpp23` (alias `cses`),
`qoj-gcc15-cpp23` (alias `qoj`), `kattis-gcc15-gnupp23` (alias `kattis`),
`yukicoder-gcc16-cpp23` (alias `yukicoder`), `yukicoder-gcc16-gnupp23`
(no alias), `library-checker-gcc15-cpp23` (alias `library-checker`) and
`timus-gcc13-cpp23` (alias `timus`).
QOJ's published C++26 option is deferred:
the audited A6 ADL name registry ends at C++23 and disables pruning for C++26.
The profile schema, dialect model and standard header/macro inventories accept
C++26; GCC 15.2's `__cplusplus = 202400L` already matches the dialect model.
Future alias moves belong in release notes. Required fields are
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
not version/triple similarity. `snapshot_compiler` accepts those one-key forms
and snapshot-only `image`, `homebrew` and `winlibs` forms, and names the compiler used for an approximate
snapshot when the judge build is unknown or differs. Package values require `name=version` (including the
exact distribution revision and a distribution prefix where applicable); recipe values require an HTTP(S) URL.
Ubuntu packages use `ubuntu-YY.MM:name=version-revision`, for example
`ubuntu-24.04:g++-13=13.3.0-6ubuntu2~24.04.1`. The release, package name, upstream
version and nonempty Debian revision are required; an optional numeric epoch
precedes the upstream version. Both the C++ schema and `ci/profiles/provenance.py`
reject missing revisions, malformed releases, whitespace and wildcard versions.
Image values must match
`docker\.io/[a-z0-9]+([._-][a-z0-9]+)*(/[a-z0-9]+([._-][a-z0-9]+)*)+@sha256:[0-9a-f]{64}`
in full: explicit Docker Hub registry, at least namespace/repository, no tag,
and a lowercase 64-hex manifest or index digest. Both schema implementations
reject mutable tags, abbreviated/uppercase digests, whitespace and mixed keys.
`image` is forbidden in `build`: an image chosen for snapshot acquisition does
not establish the judge's build identity. For Kattis, for example:

```toml
snapshot_compiler = { image = "docker.io/kattis/judge-cpp@sha256:453998e7fefcf47fb86c094be4718f2986c0af3e7a079c7bd5696a60929d514d" }
```

For Codeforces, omit `build` and use:

```toml
# Judge publishes only "GNU G++23 14.2 (64 bit, msys2)"; package revision unknown.
snapshot_compiler = { package = "msys2:mingw-w64-x86_64-gcc=14.2.0-3" }
```

Homebrew values must match
`homebrew:gcc=[1-9][0-9]*\.[0-9]+\.[0-9]+@sha256:[0-9a-f]{64}` in full.
Only the core `gcc` formula, a three-component release (positive major), and a
lowercase 64-hex **bottle blob** digest are accepted; tags, formula revisions in
the version, whitespace and mixed keys are rejected by both validators. The blob
pins the formula revision, bottle rebuild and architecture; the research notes
record those separately. `homebrew` is forbidden in `build`, like `image`, and
its snapshots record `reproduced: no`. For yukicoder:

```toml
snapshot_compiler = { homebrew = "homebrew:gcc=16.2.0@sha256:6504ecae947061ffe79c9dea2f635791bf54b148d1063ec857873eac2a67d922" }
```

WinLibs values must fully match
`winlibs:[A-Za-z0-9][A-Za-z0-9._+-]*/[A-Za-z0-9][A-Za-z0-9._+-]*\.(zip|7z)@sha256:[0-9a-f]{64}`.
The two components are a release tag and asset basename in the fixed official
`brechtsanders/winlibs_mingw` GitHub repository; the digest pins the archive
**blob**. This scoped form avoids arbitrary URLs and ambiguous repository
identities while permitting either published archive format. Both validators
reject missing/uppercase/short digests, paths, whitespace and mixed keys.
`winlibs` is forbidden in `build` and its snapshots record `reproduced: no`:
the archive is snapshot provenance only. The installer supports only the
audited Timus zip below and fails on any other identity, without substitution.

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
must exactly match the profile's `snapshot_compiler` identity as `package=<value>`,
`recipe=<url>`, `image=<digest-pinned-reference>`, `homebrew=<bottle-identity>`
or `winlibs=<archive-identity>`.
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
Names audited as depending only on ABI options or nothing, listed in the model
file header, are outside the model and keep their ordinary snapshot state.
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

CSES's [official how-to](https://cses.fi/howto), reread 2026-11-17, lists
`g++ 13.3.0`, C++23 with `-std=c++23 -O2 -Wall`, and a source limit of 128 kB.
It publishes no full command, link options, macros, output limit, OS or package
revision. The profile therefore has **no `build`** and remains **approximate**.
UNVERIFIED: Ubuntu 24.04, x86_64, `x86_64-linux-gnu`, libstdc++ and glibc on the
judge are inferred from its language versions. Its submission-form label and
custom-test availability have not been confirmed. Plan to run the probe as an
ordinary submission; obtaining the output from that UI is also UNVERIFIED.
The source budget is conservatively 128000 bytes (the page's kB conversion is
UNVERIFIED); the unpublished output limit uses the documented 60000-byte default.

The CSES snapshot compiler is the exact Ubuntu package
`ubuntu-24.04:g++-13=13.3.0-6ubuntu2~24.04.1`. On 2026-11-17 the official
[noble-updates package page](https://packages.ubuntu.com/noble-updates/g++-13)
and [amd64 compiler download listing](https://packages.ubuntu.com/noble-updates/amd64/g++-13-x86-64-linux-gnu/download)
confirmed that revision, also present in the [live archive listing](https://archive.ubuntu.com/ubuntu/pool/main/g/gcc-13/). `install-ubuntu.sh` checks the runner release and amd64
architecture, checks every pinned compiler/development package revision with
`apt-cache madison`, installs with exact `package=revision` arguments, and
verifies installed versions. Missing archive revisions fail explicitly without
fallback. Other runtime packages follow the runner; this is not a judge manifest.
We use the existing **ubuntu-24.04 runner**, rather than a container, to retain
its normal CMake/Ninja compiler for diagnostic distill and the shared Windows/Linux
job structure. Only the snapshot and probe use `/usr/bin/g++-13`. CI records its
actual `-dumpmachine`; both triple comparators permit vendor-only differences.
GCC 13.3's [release source](https://github.com/gcc-mirror/gcc/blob/releases/gcc-13.3.0/libcpp/init.cc)
sets `__cplusplus` to `202100L` for C++23. The loader checks that draft value for
GCC 13 while retaining the nominal C++23 dialect. There is currently no audited
GCC 13 special-builtin registry; snapshots record `builtin-registry: none`, so
they cannot establish exhaustive builtin facts.

QOJ's [environment blog](https://qoj.ac/blog/221) and the research
[pjudge mirror](https://pjudge.ac/blog/221) describe GCC 15.2.0, Ubuntu 26.04.1
LTS, an Intel Xeon Platinum 8573C CPU, and C++ standards through C++26.
On 2026-11-17 direct fetches of both sites returned HTTP 403; the web reader
could read the official blog at its redirected `/blog/qingyu/blog/221` path,
agreeing with the supplied mirror research. The TOML records both URLs and
the access limitation. The command selects C++ explicitly, optimizes at O2,
defines `ONLINE_JUDGE`, selects the requested ISO C++ dialect and links libm.
The drivers supply `-x c++`, `std` holds the standard, the ordered flags are
`-O2`, `-DONLINE_JUDGE`, and `link` holds `-lm`. Moving the independent standard
selection ahead of these flags preserves the command's preprocessing effects.
UNVERIFIED: the judge triple `x86_64-linux-gnu`, library identities, exact
submission label, custom-test behavior and platform-wide source/output limits.
Both probe budgets use the documented 60000-byte defaults.

QOJ does not publish a compiler package or build recipe; the profile omits
`build` and stays **approximate**. Its snapshot compiler is
`ubuntu-26.04:g++-15=15.2.0-16ubuntu1`, confirmed 2026-11-17 by the official
[resolute package page](https://packages.ubuntu.com/resolute/g++-15) and the
[Launchpad published-source API](https://api.launchpad.net/1.0/ubuntu/+archive/primary?ws.op=getPublishedSources&source_name=gcc-15&distro_series=https://api.launchpad.net/1.0/ubuntu/resolute&exact_match=true&status=Published)
(resolute Release, published 2026-03-31). UNVERIFIED: that package is the one
QOJ installed. No judge package provenance follows from a matching version.
[GCC 15.2 release source](https://github.com/gcc-mirror/gcc/blob/releases/gcc-15.2.0/libcpp/init.cc)
confirms `202302L` for C++23 and `202400L` for C++26; no loader exception is
needed for GCC 15.2. The missing C++26 ADL audit, rather than that macro value,
is why `qoj` currently selects C++23.

QOJ uses a separate `snapshot-container` job on ubuntu-24.04 with
`ubuntu:26.04@sha256:d66b366dac61b575bee2aef07e9c26e8070096fbe84a3d4d3eb0da069b5d76db`.
The official [Docker Hub tag API](https://hub.docker.com/v2/repositories/library/ubuntu/tags/26.04)
returned that multi-platform index digest on 2026-11-17 (amd64 is present).
The job installs git, CA certificates, Python, CMake, Ninja and a normal C++
toolchain as root, then the exact compiler package family using the generalized
Ubuntu installer. It never needs sudo in the container. The installer checks
the identity's release and amd64 architecture and fails if any pinned revision
is unavailable. Runtime packages follow the archive; this is not a judge
manifest. The existing CSES, AtCoder and Codeforces rows stay in the runner job
with their original acquisition steps. `job_matrices()` partitions dispatch
rows; job-level conditions skip empty partitions before matrix expansion.
Thus runner jobs have no `container` key, avoiding an invalid empty container.
The QOJ snapshot and expected output remain pending branch CI capture.

Kattis's [C++ language page](https://open.kattis.com/languages/cpp) returned
HTTP 403 to both the web reader and direct fetcher on 2026-11-17. UNVERIFIED:
the supplied 2026-11-16 transcription lists option `C++` (`cpp`), GCC 15.2.0,
GNU C++23, compile flags `-g -O2`, and the ordered link options recorded in the
TOML. It has no `-D` (including `ONLINE_JUDGE`), `-march` or include paths.
The command model already accepts `-g`, so no `inert` entry is required.
UNVERIFIED: judge OS, libc, x86_64 triple and libraries; the selected
libstdc++/glibc inventories describe an inferred GNU/Linux environment.
Source/output limits are unconfirmed, so both probe budgets use the documented
60000-byte defaults. No custom-test or output-retrieval channel is known.

Kattis's public [Docker Hub latest-tag API](https://hub.docker.com/v2/repositories/kattis/judge-cpp/tags/latest),
fetched directly on 2026-11-17, reports OCI index digest
`sha256:453998e7fefcf47fb86c094be4718f2986c0af3e7a079c7bd5696a60929d514d`,
last pushed 2026-11-02, with a linux/amd64 manifest
`sha256:22da737c95a5510f0ee58d039f070e9405856c72bb3c1202d04855addac880b9`.
Use the index pin as `snapshot_compiler.image`, never `build`; UNVERIFIED:
Kattis does not document that this image is used for grading. Docker was not
run locally; its compiler and shell/package-manager compatibility remain
UNVERIFIED until CI. No confirmed incompatibility warrants the Ubuntu package
fallback at this stage.

The Kattis row joins `snapshot-container` on ubuntu-24.04. It runs as root,
bootstraps with `sh`, and requires `g++-15 -dumpfullversion` to print exactly
`15.2.0` before setup and again before snapshot capture. Existing installed
packages are held while adding diagnostic tools, preserving the image compiler,
headers and runtimes; required upgrades fail for review. The diagnostic CLI uses
`g++-15`; snapshot/probe drivers use that same image compiler. Version or triple
mismatches fail, with no compiler substitution or reproduction claim. Existing
QOJ package installation and all runner acquisition steps remain unchanged.
The Kattis snapshot and expected output remain pending branch CI capture.

yukicoder's [environment page](https://yukicoder.me/help/environments), fetched
2026-11-17, dates its environment listings 2026-11-14 11:08:09. It lists
Ubuntu 25.04, x86_64, language IDs `cpp23_gcc16` (C++23 (gcc16)) and
`cpp23_gnu_gcc16` (GNU extensions), and the banner
`g++-16 (Homebrew GCC 16.2.0) 16.2.0`. The strict command is:

```text
g++-16 -O2 -fplugin=/usr/lib/yukicoder/noavx512-gcc16.so -lm -std=c++23 -Wuninitialized -DONLINE_JUDGE -o a.out _filename_
```

The GNU variant changes only `-std=gnu++23`. Both profiles retain the published
flag order `-O2`, `-Wuninitialized`, `-DONLINE_JUDGE`; `link` contains `-lm`.
ACL under `/usr/include` and Boost 1.92 under `/usr/local/include` remain judge
system headers outside the profile, as with AtCoder.

The same page displays `judge/lang/gcc_plugin/noavx512.cc`. Its startup callback
replaces GCC's target-pragma parser and target-attribute validator hooks. It
maps AVX-512/AVX10 requests to AVX2 and listed AVX-512 `arch=` CPUs to `znver3`,
preserves `no-` requests and option order, and warns on replacements. It skips
system-header pragmas but filters attributes. **UNVERIFIED:** the deployed
plugin binary matches this source. The plugin cannot be loaded/reproduced in
CI, so `-fplugin=/usr/lib/yukicoder/noavx512-gcc16.so` is deliberately omitted
from `flags`. Both profiles are **approximate**, with **no `build`**; the
judge's precise bottle rebuild is also unknown. There is no `-march=native`
and no GCC 16 native model; the resolver already treats reserved names as
target-pragma dependent without a model.

On 2026-11-17 the [Homebrew GCC API](https://formulae.brew.sh/api/formula/gcc.json)
confirmed formula GCC 16.2.0, revision 0, bottle rebuild 1, `x86_64_linux`, blob
`sha256:6504ecae947061ffe79c9dea2f635791bf54b148d1063ec857873eac2a67d922`.
This identifies only the snapshot compiler. **UNVERIFIED:** the judge's exact
`x86_64-pc-linux-gnu` triple (inferred from Homebrew's config.guess), compiler
build identity, library versions and default include paths. `ci/profiles/homebrew.py`
fetches an anonymous GHCR pull token and the blob by digest, verifies SHA-256
before installation, and uses `brew install --force-bottle <local bottle>` for
relocation. Missing Linuxbrew at `/home/linuxbrew/.linuxbrew`, an existing GCC
keg, failed installation, changed version/receipt/compiler path or banner all
fail without substituting another compiler or building GCC from source.
Runtime dependencies follow Homebrew; they are not a judge manifest.

CI uses **ubuntu-24.04**, since Ubuntu 25.04 images are EOL. The runner supplies
glibc headers and `stdc-predef.h`; their equivalence to the judge is
**UNVERIFIED**. CI checks the exact banner and `-dumpfullversion`, logs `-v`,
`-dumpmachine`, `-print-search-dirs` and `-E -v -x c++ /dev/null`, and invokes
the compiler by absolute path. Acquisition diagnostics and snapshot/probe runs
clear `CPATH`, `CPLUS_INCLUDE_PATH`, `C_INCLUDE_PATH`, `GCC_EXEC_PREFIX`,
`COMPILER_PATH` and `LIBRARY_PATH`. Both dialects have independent matrix rows,
snapshots and expected probe outputs; GNU facts are never derived from ISO facts.
The [GCC 16.2 release source](https://github.com/gcc-mirror/gcc/blob/releases/gcc-16.2.0/libcpp/init.cc)
was audited on 2026-11-17: `builtin_array` has the same 20 names as GCC 15.2,
now in `data/builtins/gcc-16.2.txt`; both C++23 modes define `__cplusplus` as
`202302L`. Registry lookup, probes and snapshot metadata use the version key.

The [help page](https://yukicoder.me/help), fetched 2026-11-17, gives a source
limit of 128KB (131,072 bytes), so `probe_source_bytes = 131072`. It gives an
output limit of 20MB (slightly less in practice), including stderr. Keep the
conservative documented 60000-byte default output budget. The
[custom-test page](https://yukicoder.me/run) confirms login is required.
**UNVERIFIED:** an actual judge probe match; snapshots and expected outputs
must be captured by branch CI before these profiles enter the bundled catalog.

Library Checker's sources were re-fetched on **2026-11-17**, including the
immutable [language entry](https://github.com/yosupo06/library-checker-judge/blob/01fa3c63f036611cff8be611dc1c69a5605f6a70/langs/langs.toml)
at commit `01fa3c63f036611cff8be611dc1c69a5605f6a70`. Language id `cpp` is
"C++23", version label "GCC 15.2 + AC Library(1.6)", with this command:

```text
g++ -O2 -std=c++23 -DEVAL -DONLINE_JUDGE -march=native -o main main.cpp -I /opt/ac-library
```

The profile uses `std = "c++23"`, the ordered flags `-O2`, `-DEVAL`,
`-DONLINE_JUDGE`, `-march=native`, and `link = []`. Its preferred compiler
check is plain `g++`, matching the judge and docker-library image, followed by
`g++-15`. As with AtCoder, `-I /opt/ac-library` is deliberately omitted:
profile flags reject include paths, and ACL is a judge system header outside
the standard-header inventory. Its includes disable pruning. The
[Dockerfile](https://github.com/yosupo06/library-checker-judge/blob/01fa3c63f036611cff8be611dc1c69a5605f6a70/langs/Dockerfile.GCC)
clones ACL v1.6 and copies its `atcoder` directory into `/opt/ac-library`.
Its final compiler stage says only `FROM gcc:15.2`, with no digest. The
[README's Ubuntu 22.04 requirement](https://github.com/yosupo06/library-checker-judge/blob/01fa3c63f036611cff8be611dc1c69a5605f6a70/README.md)
describes the judge host; compilation runs inside the language container.

The [Docker Hub tag API](https://hub.docker.com/v2/repositories/library/gcc/tags/15.2)
was rechecked directly on 2026-11-17. Its index digest is unchanged from the
2026-11-16 research:
`sha256:3ae15afe768b06d0c0fe088d822ba5f8045c26630bdacc8d8e7713cf5d8e7289`,
`last_updated` 2026-06-11. Its linux/amd64 manifest is
`sha256:c101370f78e4a30be178c11dd18aeee64c65d617908a98157db2392ca73ab04f`.
The same index already identifies the GCC 15.2.0 scan in
`data/extensions/libstdc++.txt`. Use
`snapshot_compiler.image = "docker.io/library/gcc@sha256:3ae15afe768b06d0c0fe088d822ba5f8045c26630bdacc8d8e7713cf5d8e7289"`.
This is **approximate**, with **no `build`**: resolving today's mutable base
tag cannot identify the deployed judge image or its compiler build. CI must
verify that docker-library's `15.2` compiler reports exactly `15.2.0`.
**UNVERIFIED for the judge:** Debian trixie base, x86_64 architecture,
`x86_64-linux-gnu` triple (inferred from docker-library's `--build=$gnuArch`
convention), libstdc++/glibc versions and default include paths. CI records
`-dumpmachine`; no container or compiler was run locally.

The image row uses `snapshot-container` on ubuntu-24.04. The job resolves the
profile's preferred compiler to an absolute path, checks `-dumpfullversion`
before and after tool installation and before capture, and holds all installed
packages while adding diagnostic tools with `--no-upgrade`. This also handles
tools already installed in the GCC base image. Snapshot and probe use that same
compiler, with no archive compiler substitution or reproduction claim.

The judge CPU is unpublished. Native handling is exactly AtCoder's: shared
`snapshot.py` loads `data/native/gcc-15.2-x86.txt`, sweeps every ABI-compatible
CPU accepted by the compiler, and includes the native host dump in its check
for variation outside the model. These regression checks never infer states
from named presets. Every native-model name stays **unknown**, including names
absent on the CI host, unless explicitly fixed by a command `-D`/`-U` (none
of this profile's operations fix a model name). The container job also logs the
CPU model and GCC's native target selection, as AtCoder's runner job does.
`probe.py` retains the exact native argv and checks the resulting expected
output against the conservative model. No separate sweep implementation is
needed in the workflow; both acquisition paths call the same capture code
(design §4.3, native fixes #33/#39).

The pinned [submission handler](https://github.com/yosupo06/library-checker-judge/blob/01fa3c63f036611cff8be611dc1c69a5605f6a70/restapi/handlers_submissions.go)
accepts source lengths 1 through `1024*1024` bytes, so
`probe_source_bytes = 1048576`. No global output limit is published (limits
are per problem); retain the conservative 60000-byte default output budget.
There is no custom-test facility: use ordinary submissions only.
**UNVERIFIED:** probe output retrieval and an actual judge match. Snapshot and
expected output remain pending branch CI capture; neither is fabricated locally.

Timus's [C/C++ help](https://acm.timus.ru/help.aspx?topic=cpp), re-fetched
2026-11-17, lists Microsoft Visual C++ 2022 17.8, GCC 13.2.0 and Clang 17.0.6.
Its GCC/Clang x64 builds come from WinLibs; the compiler switch dates to
2024-01-22. The published GCC C++ command is:

```text
g++ -x c++ -std=c++23 -O2 -fno-strict-aliasing -march=native -DONLINE_JUDGE -static -Wl,-s,--stack=67108864 -o %1.exe %1
```

`timus-gcc13-cpp23` (alias `timus`) keeps the preprocessing argv in published
order, separating the dialect and linker flags. The command classifier leaves
`-fno-strict-aliasing` unmodelled; it changes optimization alias analysis rather
than preprocessing, so it is explicitly documented in `inert`. The existing
link schema accepts both `-static` and this `-Wl,` option. The profile selects
the same A1 inventories as Codeforces: `mingw-w64` and `libstdc++`.

**Timus live probe, 2026-11-17:** user submission to problem 1000 with the
GCC 13.2 C++ option, values from `#pragma message` in the compilation-error
log (undefined names printed as themselves; no account identity recorded):

```text
__VERSION__ = "13.2.0"
__GNUC_PATCHLEVEL__ = 0
__cplusplus = 202100L
__STRICT_ANSI__ = 1
_UCRT = (defined, empty)                 [after <cstdio>]
__MSVCRT__ = 1                          [after <cstdio>]
__MSVCRT_VERSION__ = 0xE00               [after <cstdio>]
__MINGW64_VERSION_STR = "11" "." "0" "." "1"
_WIN32_WINNT = 0x0601
__USING_MCFGTHREAD__ = undefined
_REENTRANT = 1
__SEH__ = 1
ONLINE_JUDGE = 1
__OPTIMIZE__ = 1
__AVX2__ = undefined
__AVX512F__ = undefined
__SHSTK__ = undefined
__clang__ = undefined
```

This confirms strict ISO C++23 (GCC 13 reports the provisional `202100L`), UCRT,
MinGW-w64 11.0.1, POSIX threads and SEH. CRT/header observations do not become
source-entry facts. The three missing ISA macros are **observations only** and
never fix native model states. `__VERSION__` contains no WinLibs build string,
so the exact release revision remains unknown. The profile has **no `build`**
and is **approximate**. The judge's exact `x86_64-w64-mingw32` triple spelling
is UNVERIFIED; CI records the archive compiler's actual `-dumpmachine`.

The [GitHub releases API](https://api.github.com/repos/brechtsanders/winlibs_mingw/releases)
was enumerated on 2026-11-17 (100 releases per page back through 2023).
The latest matching UCRT/POSIX/SEH x86_64 revision on or before 2024-01-22 is
[`13.2.0posix-17.0.6-11.0.1-ucrt-r4`](https://github.com/brechtsanders/winlibs_mingw/releases/tag/13.2.0posix-17.0.6-11.0.1-ucrt-r4),
packaged and published 2024-01-15. r3 was published 2023-11-21; r5 followed
2024-02-03, too late for the switch. The r4 tag includes both LLVM and GCC-only
assets; choose the smaller GCC-only
`winlibs-x86_64-posix-seh-gcc-13.2.0-mingw-w64ucrt-11.0.1-r4.zip` (222024256 bytes).
Its API `digest` is null. The adjacent
[`.zip.sha256`](https://github.com/brechtsanders/winlibs_mingw/releases/download/13.2.0posix-17.0.6-11.0.1-ucrt-r4/winlibs-x86_64-posix-seh-gcc-13.2.0-mingw-w64ucrt-11.0.1-r4.zip.sha256)
supplies `6e2bc5d50c9b2775193a5f9f41939de952ee5b6a550e9c3375bc52dda6162771`.
The release notes confirm GCC 13.2.0, MinGW-w64 11.0.1, POSIX and UCRT;
the asset identifies SEH. This is the closest dated snapshot choice, not proof
that Timus deployed r4.

On `windows-2025`, `ci/profiles/winlibs.py` downloads and hashes that archive
before extraction, refuses an existing destination, and checks the absolute
`mingw64/bin/g++.exe` for `-dumpfullversion = 13.2.0`, POSIX thread configuration,
UCRT and the x86_64 target in `-v`. A separate preprocessing-only header check
confirms MinGW-w64 11.0.1, UCRT, POSIX and SEH. CI logs `-v`, `--version`,
`-dumpmachine`, `-print-search-dirs` and `-E -v -x c++ NUL` search paths.
Snapshot and expected-output capture use that absolute executable, with PATH
limited to its bin directory and Windows system directories and with injected
GCC/include/library environment paths cleared. The MSYS2 installer remains
exclusive to Codeforces; no runner compiler can be selected as a substitute.
GCC's [documented configure option](https://gcc.gnu.org/install/configure.html)
is `--enable-threads=posix`; the check accepts that or `--with-threads=posix`,
requires `Thread model: posix`, and checks WinLibs' `x86_64-ucrt-posix-seh`
package identity rather than assuming GCC prints a CRT configure option.
The historical [WinLibs GCC recipe at 28d7b327](https://github.com/brechtsanders/winlibs_recipes/blob/28d7b327ad6ade3d92a124bee1e383c6c56bd46d/recipes/gcc.winlib)
(2024-01-12, GCC 13.2.0) confirms that option spelling and package identity
construction; this is a check on the snapshot compiler, not judge build proof.

Without a native model, both `snapshot.py` and the loader in
`src/core/profile/facts.cpp` reject this host-dependent command. The fallback
in `src/core/resolve/resolve.cpp` treats all reserved names as feature names for
invalidation when a registry is missing; that is not snapshot acquisition or
an initial-state guarantee. Timus therefore adds
`data/native/gcc-13.2-x86.txt`, audited from `releases/gcc-13.2.0`'s
`i386-c.cc`, `c-cppbuiltin.cc`, `driver-i386.cc` and `cppbuiltin.cc`. Its header
lists the CPU-independent ABI names excluded from the model, as for GCC 15.2.
The audit includes GCC 13's Knights Landing/Mill and AVX-512 spellings that
were later removed. Every model name is emitted **unknown**, even if always
defined/absent in the named-CPU sweep or host dump; the sweep plus host only
reject variation outside the audited inventory. The loader requires every
model record and `initialState` preserves these unknowns, including overlapping
allow-listed names such as `__SIZEOF_FLOAT80__` and `__FLOAT_WORD_ORDER__`.
`ONLINE_JUDGE` remains an explicit command fact. No judge/CI CPU observation
narrows this bound. `data/builtins/gcc-13.2.txt` audits all 17 `builtin_array`
entries from the release's `libcpp/init.cc`; `__has_embed`, `__has_feature` and
`__has_extension` are absent. GCC 13.3 has its own key: CSES remains unchanged.

The [judge guide](https://acm.timus.ru/help.aspx?topic=judge&locale=en) describes
an output limit with a large margin but supplies no numeric limit; no source
limit was found. Both probe budgets retain the documented **60000-byte defaults**.
There is no custom-test facility. A deliberately failing ordinary submission
does expose `#pragma message` output in its compilation-error log, as confirmed
by the dated live probe; the guide says the report is available for 15 minutes.
However, `--judge-probe` currently emits runtime stdout with manifests, hex
encoding and checksums. It cannot be used unchanged through compilation-error
logs, nor can manual pragma messages be passed to `--judge-check` as protocol
output. A full runtime probe match on Timus and its output retrieval remain
UNVERIFIED. Both snapshot and expected-output artifacts await branch CI.

**Adding a profile:** a TOML names its snapshot and expected probe output, and
the embedded catalog requires both. Push the TOML and CI job on a branch,
dispatch `profiles.yml` on that branch for the new profile (it loads the TOML by
path), review the two uploaded artifacts, and commit them byte for byte to the
same branch before opening the PR, recording the run URL. Never edit the TOML
after capture without regenerating: the expected output binds its digest.

All jobs upload snapshots for review; they never commit or publish changes.
Bundled snapshots are validated by embedded-profile tests. The four
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
to **60000 bytes**. Codeforces, AtCoder, QOJ, Kattis and Timus currently use this conservative
fallback: no published source/output limit has been established here, and these
values are not claims about those judges' limits. An override must cite the
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

`profiles.yml` first builds the LLVM-free `DISTILL_PROFILE_ONLY` diagnostic CLI
with the normal toolchain. Immediately after each snapshot step it runs
`ci/profiles/probe.py` with that job's snapshot compiler. This loads the freshly generated snapshot by explicit
TOML path, finds a partition satisfying both budgets for every part, compiles
and runs all probes, checks the complete result, and uploads the expected file
alongside the snapshot. It uses the snapshot's exact `-x c++ -std=<std> <flags>`
argv, retaining ordered macro operations and native options. Linker-only
`link` options are excluded as in snapshot capture: no judge-installed external
library is needed by this diagnostic. The AtCoder recipe build, Codeforces exact
MinGW package job and CSES exact Ubuntu package job follow this path. Do not substitute a local
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

## Maintenance

The serialized [profile maintenance workflow](../../.github/workflows/drift.yml)
checks evidence and complete artifact downloads weekly, and regenerates profiles
from a cold start monthly. Dispatch can select claims and full profile names.
Issues named `drift: <profile>: evidence|availability|reproduction` describe
separate claims: deployment/listing facts, obtainability with digest verification,
and byte equality with both shipped snapshot and probe output. Shared host
outages also appear in `drift: upstream: <host>`. Read the per-source status,
last success and failure counters in the body; only `same facts` passes evidence.
Listing availability never proves judge adoption. Closed tracking issues retain
state; do not delete their machine-readable comments. Availability alerts close
after verified recovery; evidence/reproduction alerts require human closure.
Four unavailable weekly windows require manual review by the recorded deadline.

Investigate changes against the original-language sources and run a judge probe
where possible. If the acquisition artifact vanished, submit a re-snapshot PR
using a successor artifact, documenting provenance and reviewing all byte
changes, or mark the profile **not regenerable** with the reason in its profile
comments and this README. Committed snapshots remain usable. A judge compiler or
option upgrade gets a **new profile name**; preserve the old name's meaning and
document alias moves. Do not refresh `updated` for an automated pass: it records
human review. The CLI suggests `--judge-check` after more than 180 days.

The [framework documentation](../../ci/drift/README.md) defines watch/result
schemas, the extractor interface and issue state model. Part 2 adds real watch
files and per-judge extractors; currently evidence reports `no sources`.
Ubuntu acquisition uses [recorded snapshot timestamps](../../ci/profiles/ubuntu-snapshots.md).
