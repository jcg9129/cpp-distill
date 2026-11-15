<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Progress

Where cpp-distill stands, so a new session can continue from the repo alone.
Design: [`design.md`](design.md) (approved v6). Reviews of each design round:
[`reviews/`](reviews/).

## How work is done

- Every design or significant decision is challenged by Codex (`codex-run
  ask`) before it is final. Codex writes the code (`codex-run work`), Claude
  (Opus) reviews the diff, coordinates and reports to the user.
- Claude pushes feature branches, opens PRs and merges them when CI is green,
  as squash merges unless splitting makes sense.
- LLVM is only ever built on GitHub runners. The development VM is shared and
  small: one heavy job at a time.

## Milestones

| M | State |
|---|---|
| D0 design | done: v6 approved 2026-11-15 after five Codex rounds |
| D1 distribution spike | done: SDK `llvm-23.1.5-1` published for all five platforms; spike passes everywhere (results in [`d1-spike.md`](d1-spike.md#results)) |
| D2 preprocessing half | D2a–D2c implemented; resolver, validation configurations and the `--no-prune` CLI remain |
| D2a command/process/probe | done (#5): C++20 core with unit tests, green on Linux (posix_spawn and fork paths), macOS and Windows CI |
| D2b scanner/names/inliner | done: text scanner, embedded C++26/C23 name tables, include search and guarded recursion, line-preserving raw output, unit and GCC equivalence/runtime case runner |
| D2c judge profiles and A1 lists | done: strict TOML subset/schema and injected lookup, researched Codeforces/AtCoder profiles, CI-produced snapshots and all four merged A1 inventories shipped; embedded-profile/list and generator tests |
| D3 pruning | after D2; needs a short Codex round on the v6 pruning rules first |
| D4–D5 | see design §7 |

## D1 findings (2026-11-15)

- The official LLVM 23.1.5 archives (Linux x86_64/arm64, macOS arm64,
  Windows x86_64; none for macOS x86_64) contain every library we need, and
  Windows uses the static CRT (LIBCMT). But their CMake exports require zlib
  and zstd at link time (`LLVMSupport` links `ZLIB::ZLIB` /
  `zstd::libzstd_static`), so using them means bundling those statically.
  Our own SDK builds disable both.
- GitHub API calls from runners must be authenticated: anonymous calls hit
  the shared 60/hour limit.
- Workflows with `workflow_dispatch` must be on `main` before they can be run;
  the token needs Actions: read and write.
- Our static SDK builds take 42–98 min per platform on the free runners and
  stay under 3 GB peak RSS per process; archives are 68–103 MB. The linked
  spike executable is 40–86 MB.
- The release `publish` job's `GITHUB_TOKEN` is refused (HTTP 403) when it
  creates a release, cause not yet known; see [`d1-spike.md`](d1-spike.md#results).
- AlmaLinux 8 images ship `coreutils-single`; don't install `coreutils` there.


## D2c follow-up (2026-11-15)

- Shipped profiles include the byte-identical CI snapshots: Codeforces from
  `profiles.yml` run 43771140647 (approximate), AtCoder from run 43768705680
  (reproduced). Missing named snapshots still fail instead of silently downgrading.
- Codeforces: GCC 14.2 MSYS2/MinGW-w64, `c++23`; AtCoder: GCC 15.2.0,
  `gnu++23`, after the June 2026 update. Judge sources and inferred facts are
  recorded in `data/judges/`. AtCoder's reproduced build confirms its target
  spelling; its OS release and judge-side architecture inference, plus Codeforces'
  target spelling and patch level, retain their UNVERIFIED notes.
- All four merged A1 lists (glibc, libstdc++, mingw-w64 and macos-libc) from
  `extensions.yml` run 43768709866 are shipped byte for byte, replacing local-only
  scans and placeholders. Every inventory is nonempty. Linux C++ profiles select
  both glibc and libstdc++; default no-profile recursion uses all four inventories.
- Remaining D2 work: resolver state/closure/evaluation and adversarial cases;
  validation configurations including preprocess checks; the usable `--no-prune`
  CLI. D2c supplies components, without wiring CLI judge selection prematurely.
- FIX1 implements the approved v7 amendment: ordered compile flags, separate link
  options and build provenance; strict per-name snapshots; four-state facts with
  reproduced/approximate modes and a commented approximate allow-list; embedded
  GCC 14.2/15.2 special-builtin and x86 native-dependency inventories.
- Snapshot tooling uses the full effective command, line markers for startup
  provenance (stdc-predef.h only), special-builtin probes and native CPU sweeps
  with outside-model failures. `libraries` explicitly selects independent sets.
- Extension generation now follows textual include/include_next reachability from
  the standard-header list and all ext/pb_ds headers, including inactive includes.
  Merged CI results: glibc 1505 names, libstdc++ 167, mingw-w64 409,
  macos-libc 600. Computed includes are not followed.
- Codeforces publishes only "GNU G++23 14.2 (64 bit, msys2)"; its MSYS2
  package revision is unknown. The profile omits `build` and uses available GCC
  14.2.0-3 as `snapshot_compiler`, producing an approximate snapshot with
  `reproduced: no`. All 16 locked compiler/dependency archives were downloaded
  and their `.PKGINFO` depend/provides closure checked against the live listing.
  CI validates that closure before installing exact files with `pacman -U`.
- AtCoder CI builds GCC 15.2.0 from the published recipe on ubuntu-24.04, using its
  configure options and a recipe-hashed compiler cache. Run 43768705680 confirms
  `-dumpmachine` is `x86_64-pc-linux-gnu`.
- Embedded-profile tests cover judge facts, command/A4/A5 provenance, approximate
  allow-list behavior (including `__has_include` definedness) and native uncertainty.
  Embedded-list tests load all inventories and check extension and user macro names.
- Resolver and validation orchestration (§4.5–4.6) remain subsequent D2 implementation;
  target verification will require exact probes and no unresolved native host.
