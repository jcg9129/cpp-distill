<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Progress

Where cpp-distill stands, so a new session can continue from the repo alone.
Design: [`design.md`](design.md) (approved v8). Reviews of each design round:
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
| D0 design | done: v7 approved 2026-11-15 after five Codex rounds and the profile amendment |
| D1 distribution spike | done: SDK `llvm-23.1.5-1` published for all five platforms; spike passes everywhere (results in [`d1-spike.md`](d1-spike.md#results)) |
| D2 preprocessing half | implemented through D2e: configurations, preprocess checks and usable `--no-prune` CLI; SDK link/run verification belongs to CI |
| D2a command/process/probe | done (#5): C++20 core with unit tests, green on Linux (posix_spawn and fork paths), macOS and Windows CI |
| D2b scanner/names/inliner | done: text scanner, embedded C++26/C23 name tables, include search and guarded recursion, line-preserving raw output, unit and GCC equivalence/runtime case runner |
| D2c judge profiles and A1 lists | done: strict TOML subset/schema and injected lookup, researched Codeforces/AtCoder profiles, CI-produced snapshots and all four merged A1 inventories shipped; embedded-profile/list and generator tests |
| D2d resolver | implemented: four-state walk/joins and isolated condition inputs, optional Clang evaluator, test-only GCC oracle, adversarial cases and Clang/differential CI |
| D2e validation/pipeline/CLI | implemented: exact configuration probes, directional satisfaction, target/local verification, preprocess checks, no-prune orchestration/report, bounded Clang workers and shared core/CLI cases |
| D3 pruning | D3a analysis/model/A6 registry and D3b rendering/invariants/stripping/pruned pipeline/CLI implemented; LLVM linking and real extraction/CLI runs remain CI-only; D3c adversarial expansion remains |
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

## D2d resolver (2026-11-16)

- `resolve/` turns inliner output into line-preserving flat text, with decisions
  for every conditional directive, external macro/arithmetic requirements,
  reason counts, disabling reasons and the unknown-in-user-code precondition.
  Header and GCC option-pragma invalidation use the embedded A1/native inventories.
- Reuses profile facts and the probe's full macro-definition type. A3 conflict
  checking also catches definitions in approximate snapshots whose values are
  withheld. Known source bindings are derived facts; unknown alternatives start
  independently and join equal bindings.
- Optional `distill_clang` uses Clang 23.1.5's in-memory preprocessor. The core
  still builds without LLVM. Test-only GCC evaluation, fake logic tests and
  resolver cases run in existing CI jobs; the new `resolver (linux, clang)` job
  builds inside manylinux_2_28 and runs Clang plus differential suites.
- Cases capture actual local GCC facts for equivalence, replace local macro
  operations with fixture operations, compare every preprocessed token's file/line,
  and compare runtime stdout/exit. Shipped profiles only assert decisions. One
  deliberately invalid standard-header fixture must fail both equivalence checks.
- Arithmetic without a target model is deliberately limited to Boolean token
  trees; unsupported integer layouts and macro-generated `defined` stay unknown.
  Full worker/process containment for in-process Clang awaits pipeline/CLI wiring.
- Local verification and proposed design wording are recorded in temporary root
  `D2D-SUMMARY.md`; approved `docs/design.md` is unchanged.
- Next: D2e validation configuration orchestration and the usable `--no-prune` CLI.


## D2e validation and no-prune CLI (2026-11-16)

- `validate/` probes exact effective flags, ordinary/special macros and startup
  provenance, constructs resolver initial states using the snapshot fact rules,
  reruns directional satisfaction, and distinguishes target from local checking.
  Local native options always prevent target verification. Without a profile,
  resolution retains D2d's conservative facts and uses only the effective dialect.
- The production preprocess parser compares tokens, logical locations, user file
  identities and pragma boundaries. System headers compare by spelled path.
  Raw checks compare with the original; flat checks compare with raw under the
  selected satisfying configuration. Failed flat checks select checked raw;
  failed raw checks retain work files, report the path and return exit 1.
- `pipeline/` orchestrates the no-prune stages without LLVM, with injected
  evaluator/runner, shared phase deadlines, bounded outputs and exact checked
  bytes. `report/` supplies the non-pruning header and every conditional record.
- SDK-only `distill` supports output, judge name/file, report, strict, verbose,
  mandatory no-prune, help and exit codes 0/1/3. Clang evaluations run in bounded
  process-group/Job Object workers. Runtime remains not requested (D4).
- Shared pipeline cases cover no profile, captured reproduced/approximate facts,
  absent/present check compilers, native flags, disabled resolution, A3 conflict,
  local macro replacement, baseline/raw failures, token corruption, unknown/dead
  directives and strict results. The unchanged resolver CI job runs them through
  the actual CLI as well as the core Clang evaluator, plus CLI surface checks.
- D2 is implemented; LLVM linking, the real CLI run, and cross-platform behavior
  still require CI. Local normal/fork suites and published-SDK syntax checks are
  recorded in temporary `D2E-SUMMARY.md`. No design edits were made; proposed
  wording for implementation ambiguities is collected there for review.

## D3a pruning analysis (2026-11-16)

- Added an LLVM-free pruning model, preconditions, union-find retention closure
  for rules 1–5, static name invariant, strict worker protocol and separate
  analysis report with policy/registry identity and byte/cause attribution.
- A6 registry is embedded, versioned for C++20/23 and SHA-256 identified.
  Stable clauses were checked against N4861/N4950; N4861's exact error-code
  ADL permission remains explicitly UNVERIFIED and conservatively retained.
- The selected flat configuration supplies the parse target/search/dialect and
  ordered macro operations; unsupported semantic translations disable pruning.
  SDK-only extraction uses embedded resource headers in a bounded child process,
  physical ownership and D2 marker provenance, PP callbacks/token watching,
  AST/association/system edges, dependent names and syntactic inertness facts.
- Unsupported asm/attributes/dependent qualified or member lookup/pragmas,
  recovery/error ASTs, foreign macro pasting and unknown facts fail closed.
  GCC 14.2/15.2 PBDS inspection is incomplete: explicitly not covered, so PBDS,
  rope and other unaudited extension includes disable pruning.
- Core model tests run in every required unit job. The existing resolver job
  additionally builds the worker and checks extraction cases directly, including
  cross-target ADL retention/refusal and Mint retained-byte measurement.
- Local verification, counts, conservative decisions and proposed design wording
  are recorded in temporary root D3A-SUMMARY.md. No design changes were made.
  LLVM linking/extraction execution require CI. D3b/D3c still own rendering,
  comment stripping, pruned CLI/pipeline integration and the adversarial suite.

## D3b rendering, checked pruning pipeline and CLI (2026-11-16)

- Added LLVM-free rendering from immutable byte ranges, safe separators and
  generated-marker reconstruction. Independent re-lexing checks retained token
  spellings/locations and source-directive boundaries/order; the static name
  invariant checks final candidate bytes. Rendering maps physical provenance.
- `--strip-comments` transforms raw, flat and pruned before their checks. It
  preserves multiline directive semantics, resynchronizes following lines and
  keeps comments where a skipped marker cannot restore a later `#elif` location.
  Keep annotations bind to physical declaration occurrences before stripping and
  survive resolution/edit mapping separately from their comments.
- The pipeline derives pruning from the selected checked flat configuration,
  merges analysis/coverage/attribution into the report, builds pruned without user
  include paths, and falls back to exact checked flat bytes on rejection.
  Its internal sample API compares baseline/raw, same-configuration original/flat,
  and flat/pruned stdout and exit codes under bounded processes.
- The CLI prunes by default, supports repeated indexed `--keep` and stripping,
  rejects unknown keep names with exit 1 and retains D4's `--verify-run` rejection.
  Strict requires pruned/target. Its own private extraction mode preserves the
  single-binary distribution, and output uses a staged same-filesystem rename.
- Policy D3b-1 explicitly treats intrinsic `__LINE__`/`__FILE__` as location facts
  certified by rendering; definitions and external pasting retain their checks.
  Reports separate family-owned and structural source bytes and retain rejected
  analysis without certifying the selected fallback.
- Required core jobs cover byte-exact rendering and separator pairs plus synthetic
  pruning-worker pipeline faults. The unchanged resolver job adds real-worker
  and real-CLI pruning cases, including typical_library, target fixture, refusals,
  line markers, keep/strip combinations and deliberately bad removals. The CLI
  runner independently compares its published bytes with checked flat execution.
- Local normal/fork test counts, SDK-only syntax checks, design ambiguities with
  proposed wording, and CI-only limits are recorded in root `D3B-SUMMARY.md`.
  `docs/design.md` remains unchanged. D3c retains the broader adversarial suite.
