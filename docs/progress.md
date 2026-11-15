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
| D1 distribution spike | in progress: workflows merged (#1–#3); first SDK build and official-archive audit running |
| D2 preprocessing half | next |
| D3 pruning | after D2; needs a short Codex round on the v6 pruning rules first |
| D4–D5 | see design §7 |

## D1 findings so far (2026-11-15)

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
