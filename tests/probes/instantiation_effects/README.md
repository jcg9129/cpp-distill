# Instantiation probes under A11

Run `timeout 120s python3 tests/probes/instantiation_effects/run.py` from the
worktree. Each compile/run also has its own timeout; output stays in `.checks`.

These probes are kept as demonstrations of effects excluded by A11 (user,
2026-11-20), rather than required preservation tests for policy D3d-6:

| Probe | A11 exclusion |
| --- | --- |
| `friend_address.cpp` | Compilation/link success depending on friend-definition instantiation, including direct self parameter shapes |
| `friend_lookup.cpp` | Friend lookup and definition availability via carrier instantiation |
| `friend_deduction.cpp` | Return deduction/lookup depending on friend-definition instantiation |
| `parameter_object.cpp` | Runtime destruction of template parameter objects; constant-destruction validity is still a language requirement |
| `unseen_argument.cpp` | Potential class-valued parameter objects absent from locally known bindings; A11 now excludes their destruction regardless of binding |

A11 does not exclude non-inert template static/thread storage or templated
attribute/symbol emission. Those remain pruning regressions in `tests/cases/prune`.
The friend-dependent fixtures there use `a11-exclusion | explanation` to check
allowed removal and the A11 report line while running only the original oracle.
Other fixtures retain original/rendered equivalence checks.
