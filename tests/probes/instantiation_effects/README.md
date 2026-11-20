# Instantiation probes under A11

These bounded compiler probes document the extended A11 boundary (user,
2026-11-20). They demonstrate why the exemptions need an assumption; observed
compiler behavior alone is not a portable proof.

| Probe | A11 clause covered |
| --- | --- |
| `friend_address.cpp` | Compilation/link success depending on friend-definition instantiation, including direct self parameter shapes |
| `friend_lookup.cpp` | Friend lookup and definition availability via carrier instantiation |
| `friend_deduction.cpp` | Return deduction/lookup depending on friend-definition instantiation |
| `parameter_object.cpp` | Destruction of template parameter objects; constant-destruction validity remains a language requirement |
| `unseen_argument.cpp` | Destruction of potential class-valued parameter objects absent from locally known bindings |
| `lifetime_reference.cpp` | Destruction of a temporary whose lifetime a constexpr reference extends; `PICK` models the `sizeof(long)==4` selection |
| `lifetime_aggregate.cpp` | Destruction of a temporary whose lifetime a constexpr aggregate's reference member extends; also includes `PICK` |
| `block_static.cpp` / `block_static.py` | Destruction of never-reached block-scope static/thread storage, including non-constexpr variables and constexpr-destructor variants |

Run `timeout 120s python3 tests/probes/instantiation_effects/run.py` from the
worktree. Each compilation has a 20-second deadline and execution five seconds;
outputs stay in `.checks/probes`. GCC accepts the non-trivial constexpr
extended-temporary probes and prints `main` then `destroy`; `DROP_TRIGGER`
removes the destruction. Clang rejects those constant expressions as unsupported.
The locally trivial `PICK` form compiles with both compilers, while the other
target can need non-trivial destruction. This is an A11 exclusion, not evidence
that a locally trivial selection proves all-target trivial destruction.

Run `timeout 180s python3 tests/probes/instantiation_effects/block_static.py`
for the block-storage investigation. Assembly and JSON stay in
`.checks/block-static`; compiler/runtime deadlines are 20/five seconds.
The matrix checks installed GCC 15, Clang 22 and GCC 13, C++17/20/23,
`-O0`/`-O2`, template/non-template, static/thread storage, never-reached and
twice-reached controls, and `constinit` controls in C++20/23. The C++20 constexpr
destructor uses `!std::is_constant_evaluated()`; C++23 uses `if !consteval`;
C++17 tests ordinary destruction. GCC 15 and Clang 22 never destroy the
never-reached objects tested here. The standard does not establish that
exemption for constant-initialized block storage; D3d-7 relies on A11.

Pruning fixtures use `a11-exclusion | explanation` to assert allowed removal
and `report | pruning assumption: A11`, while running the original oracle.
Non-constexpr namespace-scope template storage with dynamic initialization or
possible non-trivial destruction remains a retention regression, including
non-constexpr reference/aggregate lifetime extension. Those fixtures still
check original/rendered equivalence. Templated attribute/symbol effects also
remain sources.
