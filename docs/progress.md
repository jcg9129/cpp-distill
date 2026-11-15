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
| D2c judge profiles and A1 lists | implemented: strict TOML subset/schema and injected lookup, researched Codeforces/AtCoder profiles, snapshot workflow, embedded A1 lists and generator tests; matching judge snapshots and Windows/macOS lists await workflow artifacts and PR review |
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

- Shipped profiles intentionally report **snapshot missing** until matching GCC
  workflow artifacts are reviewed; they never silently become allow-list-only.
- Codeforces: GCC 14.2 MSYS2/MinGW-w64, `c++23`; AtCoder: GCC 15.2.0,
  `gnu++23`, after the June 2026 update. Judge sources and inferred facts are
  recorded in `data/judges/`. Exact target spellings and Codeforces patch level
  need confirmation against judge evidence.
- Local A1 lists cover Ubuntu glibc headers and g++ 15 libstdc++; Windows/macOS
  placeholders disable protection conservatively. Linux C++ profiles select
  both the glibc and libstdc++ inventories explicitly. Default no-profile recursion is conservative
  until every library inventory is available; tests use complete synthetic sets.
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
  Local results: glibc 1461 names, libstdc++ 149. Computed includes are not followed.
- Codeforces publishes only "GNU G++23 14.2 (64 bit, msys2)"; its MSYS2
  package revision is unknown. The profile omits `build` and uses available GCC
  14.2.0-3 as `snapshot_compiler`, producing an approximate snapshot with
  `reproduced: no`. All 16 locked compiler/dependency archives were downloaded
  and their `.PKGINFO` depend/provides closure checked against the live listing.
  CI validates that closure before installing exact files with `pacman -U`.
- AtCoder CI builds GCC 15.2.0 from the published recipe on ubuntu-24.04, using its
  configure options and a recipe-hashed compiler cache. Its inferred
  x86_64-pc-linux-gnu triple stays UNVERIFIED pending actual workflow output.
- No judge snapshots or remote workflow runs were manufactured locally. Resolver
  and validation orchestration (§4.5–4.6) remain subsequent D2 implementation;
  target verification will require exact probes and no unresolved native host.
