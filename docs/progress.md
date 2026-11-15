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
  placeholders disable protection conservatively. Linux C++ protection composes
  the glibc and libstdc++ inventories. Default no-profile recursion is conservative
  until every library inventory is available; tests use complete synthetic sets.
- Remaining D2 work: resolver state/closure/evaluation and adversarial cases;
  validation configurations including preprocess checks; the usable `--no-prune`
  CLI. D2c supplies components, without wiring CLI judge selection prematurely.
- Design gap to resolve before relying on A4: the brief's snapshot recipe passes
  only `-std`, but AtCoder uses `-march=native`, `-O2`, `-pthread`, `-fopenmp`, etc.
  GCC version/triple alone do not reproduce those predefined macros or prove
  identical compiler build defaults. See `data/judges/README.md`.
- A second design gap: GCC's `-dM` output excludes special builtins such as
  `__LINE__`, `__FILE__`, `__COUNTER__`, `__has_include` and `__has_builtin`;
  local `defined(...)` checks confirm they exist. The resolver must model these
  separately before interpreting snapshot absence as undefined (and extend A3
  checking to them if reserved user definitions are supported).
