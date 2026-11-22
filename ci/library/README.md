<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# L1c CI data pipeline

Generator 5 extends the approved L1c design to `__cpp_lib_*` and object-like
names in `data/names/standard-macros.txt`. Capture expands complete identifier
chains, retaining noninteger replacements as unusable evidence. Only a single
integer literal is usable; parentheses, unary minus and other expressions stay
unknown, and function-like definitions are not captured. The source compiler and
ordered argv are always the profile's own. Standard value aliases to compiler
predefines are audited against those exact inputs, including native uncertainty.
The narrow sole-writer initial-undef rule handles GCC `float.h`; later undefs and
multiple writer files still fail closed. Full evidence retains `function_like`
write metadata and per-root `reset_prefixes`. The finite source-entry absence
rule for `__STDC_VERSION__` and `__STDC_WANT_IEC_60559_TYPES_EXT__` handles GCC
13's C++ version gate and IEC opt-in; command/startup overrides and any reachable
header write revoke it. The evidence records `source_entry_absences`.
Snapshot `function_like` lists retain presence only, so a name that is object-like
in another capture cannot be misclassified as absent. They supply no values and
function-only names do not enlarge the resolver's universe.

Generator 5 also requires a compatible captured target and strict ordered flag
matching (apart from the existing
harmless allowlist) for approximate/satisfaction agreement. This rejects both
borrowing a flagged capture for an unflagged profile and borrowing default values
for `-m32`/`-mfpmath=387` profiles. Generators 3/4 remain readable with their legacy
feature-only policy until the widened CI artifact is imported.

DLIB emits integer values normally and `<unsupported>` for recorded noninteger
replacements, without expanding them in a diagnostic TU. That marker reports
presence only; it does not validate the replacement or license a fact.
`<function-like>` serves the same purpose for excluded function-like definitions
of names otherwise present in the object-like universe.

Design: [§4.3.3](../../docs/design.md#433-library-feature-test-macro-facts-l1c).
Shipped evidence is generated in CI. Offline tests use synthetic text and mocked
subprocesses; local compiler benchmarks write only to `out/`. The resolver uses
the reviewed embedded baseline; diagnostics never license facts.

`profiles.yml` invokes the L1c capture after each predefined snapshot with the
same executable, dialect and flags. Each `snapshot-<profile>` artifact adds
`<profile>-library.json`. `extensions.yml` captures the same evidence from its
reachable-header scans (Linux/MinGW libstdc++ rows); its artifacts add
`<label>-library.json`. macOS rows scan libc rather than libstdc++ and explicitly
set `library: false`. Profiles supply GCC 13 coverage as well as the newer
versions. The old AlmaLinux compiler uses C++17, GCC 11–12 compatibility scans
use C++20 and GCC 13+ use C++23; profile dialects are never substituted.

`library.yml` calls both workflows and downloads their artifacts. Profile
artifacts use the separate `library-snapshot-` namespace in this caller, allowing
`drift.yml` to request monthly reproduction and L1c in one run without name
collisions. The merger reads both matrix sources and requires exactly one
report per eligible row. It fails for missing, duplicate, extra or unsupported
reports; additions cannot silently be excluded. It regenerates the entire data
set without merging committed facts and compares it byte for byte.
PR and manual captures use the AtCoder compiler cache by default. Only the weekly
scheduled L1c drift call passes `cold_start: true`; monthly reproduction retains
its separate cold capture.

The generated artifact contains two files (schema 2, generator 5):

- `library.json`: the compact, deterministic baseline to import into
  `data/library/`. It retains build metadata, per-header snapshot facts/status,
  per-major/root classifications and cross-build agreement. Sorted JSON has one
  record per line. A successful empty snapshot proves no selected object-like macros;
  `missing`, `error` and `disabled` snapshots prove nothing.
- `library-evidence.json`: complete build reports, including installed-header
  definitions, lexical conditions, include edges/targets, request/value support,
  compiler definitions, per-root/per-value audit results and parse failures.
  Review this uploaded file; do not commit it. Each compact build contains
  `evidence_sha256`, the full SHA-256 of its complete report serialized as ASCII
  JSON with sorted keys and separators `(',', ':')`, without a trailing newline.
  Thus evidence-only changes still fail the byte comparison.

For A12, each capture raw-tokenizes **every** file in its existing reachable
header set, including libc, inactive branches and macro definitions. It skips
comments, character/string literals (including raw literals and their suffixes)
and pp-numbers, and decodes phase-2 splices and universal character names to
the identifier spelling used by the worker. No configuration or snapshot-status
filter narrows this inventory. Roots and textual include limitations are the
same as the existing reachable-header scan, including `include_next` and PBDS.

Full evidence adds `member_inventory`: sorted unique `names`,
`reachable_headers` (the number of search-relative header keys), `token_pasting`
and sorted `token_pasting_headers` keys. Only actual raw `##`/`%:%:` tokens count;
comments/literals do not. This flag is report-only: the worker's own expansion
inventory supplies local paste results, and scanning does not attempt to predict
cross-build macro expansion.

The compact file stores one global sorted `member_names` union across **all**
eligible builds, including approximate profiles and extension scans. It does
not repeat per-build sets or add a membership matrix. Each compact build stores
`member_inventory` with `name_count`, `names_sha256` (the full SHA-256 of the
canonical sorted name array), `reachable_headers` and `token_pasting`.
Full evidence retains membership for review, and its digest still detects
per-build changes even when the global union is unchanged. This widening can
only retain more members. JSON size grows by the canonical name-array bytes
plus one small summary per build; CI prints the exact added inventory bytes.

The C++ embedding front-codes the sorted union: each newline-delimited record
stores its shared UTF-8 prefix length and the remaining suffix. Printable prefix
codes and roughly 4 KiB chunks keep source literals portable. The inventory is
decoded once on first use into one reserved string and a sorted span of views;
the views remain valid for the process lifetime. The 32,555-name baseline emits
about 588 KB of C++ source including feature transitions, down from 852 KB with
one string-view literal per member. Generator 5 widens transitions to standard
integer macros and raises the enforced source budget to 3,200,000 bytes. The
unchanged generator-4 baseline now embeds to 588,498 bytes; the actual widened
artifact size must be recorded after CI regeneration. Compiled round trips check
every shipped member name.

The compact file shares repeated records using content-address tables (the first
16 hexadecimal SHA-256 digits, with collision detection). Build `headers` refer
through `build_headers` to `snapshots`; version `roots` refer through
`root_classifications` to `classifications`; `agreement` refers through
`root_agreements` to `agreements`. Each classification contains a `default` for
names with no reachable definition, common sorted `dependencies` for its explicit
macros, and a `macro_sets` reference. A macro set has `entries`, an optional
`base` set, and optional `removed` names; apply the base, removals, then entries.
Entries reference `macro_classifications` records with `independent`, `values`
(the literal pairs independent in every build), and sorted additional dependency
names. Union these with the root's common dependencies. The default applies only
to omitted names and has its own dependencies. The `get_snapshot`,
`get_classification` and `get_agreement` helpers in `merge.py` expand these tables
for inspection. Macro-set IDs hash their expanded entry map. Bases are
deterministic and acyclic; unrelated additions do not renumber records.

The audit's unit is a fresh TU containing only `#include <H>`. For each root H,
a definition inherits its own conditions and the conditions on **all** include
paths from H to its file. A conditional alternative still counts when another
path is unconditional. Paths from other roots do not contribute, including
writes to request/value/candidate macros. Literal includes follow search order,
quoted local lookup and `include_next` search position. Files retain
search-directory index/include spelling, including directory symlink aliases;
contents are read from the resolved file.

Whole-file guards and nested `#ifndef X` / `#define X` wrappers are verified
against all installed writes and command/startup overrides. Only the actual
negative wrapper test is exempted, not arbitrary positive/value tests of X.
Conflicting writes or different file contents sharing X revoke the exemption.
A negative guard test before entering another guarded file is exempted only
when no guard writer can be reached without that include edge; otherwise the
possible earlier inclusion remains untrusted. This covers `#include_next` C
wrappers without treating `_GLIBCXX_CMATH` or `_GLIBCXX_STDLIB_H` as general
compiler inputs.

The two IEEE candidates are verified from their actual reachable definitions.
The audit also trusts these inputs, recording the rule for each in
`audit.trusted_inputs` and each root's `trusted_inputs`:

- Other `__cpp_lib_*` and `__glibcxx_*` macros whose reachable writes are
  independently justified for that same root (`independent-library-macro`).
  SCC expansion follows both conditions and request/value aliases. A queue
  proves grounding from the root: a macro needs a reachable writer and all its
  lexical/value prerequisites; an include needs its source and macro conditions.
  This licenses a later feature-conditional include returning to an already
  grounded provider. Unanchored cycles and unsupported writes remain dependent.
- `_GLIBCXX_HOSTED` only when **every** scanned build of that GCC major records
  value 1 (`all-major-builds-hosted`). Judges are hosted. One non-hosted build
  or missing observation revokes trust for the entire major.
- Literal `__has_include` / `__has_include_next` queries only when every build
  of the major records the same boolean answer (`header-presence-agreement`).
  Agreement on absence is valid; disagreement or missing evidence is not.
  A reproduced profile's header-query set is its own build. Quote lookup and
  `include_next` search position are preserved, with source-relative query keys
  that do not depend on machine-specific search-directory indexes. Computed
  operands remain untrusted.

`probe.build_inputs` captures the expanded hosted value and batches compiler
header queries by lookup context, using the discovered search order and quote
directory. Raw `observations` and `audit.header_tests` (predicate, operand, source
files) are retained in each report. A raw capture applies these rules to its own
build. At merge, the complete matrix determines the approved inputs, and the
merger reapplies them to stored, unfiltered SCC `requirements` without rerunning
the audit. Full merged evidence records the effective rules, values and build
sets, so assumptions can be reproduced or revoked from artifacts alone.

Other configuration switches never receive blanket trust. Header writes and
command/startup overrides revoke compiler-input trust for that root. Native-model
names remain untrusted under native flags. Literal include operands and
header-test arguments are not macro identifiers. A reachable computed include,
unsupported absolute include or unparsed file taints the entire root, including
absence claims; unreachable failures are listed without affecting it.

Each literal (root, name, value) is audited separately, retaining all matching
definitions and paths. Undefinitions/unsupported replacements invalidate its
pairs. Absence requires every reachable definition to be independent, or a
complete untainted closure containing no definition. Agreement still requires
all builds of the major, with their actual dialects/flags, to report the same
presence/value and an available header.

Failed acquisition or audit leaves incomplete inputs and fails the check. A
baseline difference still uploads the generated output and prints a unified
diff. The first run is expected to fail until its reviewed baseline is imported.
No job commits data, opens a data PR or refreshes `updated` automatically.

## First capture or reviewed refresh

After pushing these scripts/workflows to a branch, run (on a machine with GitHub
access; these are CI maintenance instructions, not local compiler commands):

```sh
gh workflow run library.yml --ref <branch>
gh run list --workflow library.yml --branch <branch>
gh run watch <run-id>
gh run download <run-id> --name library-generated --dir out/library-review
```

Dispatch `.github/workflows/library.yml` ("L1c library data") on the branch
containing these changes, with **no dispatch inputs**. It calls `profiles.yml`
with `profiles: all`, `artifact_prefix: library-snapshot`, `cold_start: false`,
and `extensions.yml` with its complete default matrix. Do not dispatch only a
single profile or combine generator-3/4 artifacts with generator-5 captures. The
first comparison will fail because the committed generator-4 baseline has only
feature-macro facts; `library-generated` is uploaded on this expected difference.

Review the failed check's compact diff and both files: per-build status,
per-root independent pairs, dependencies, full evidence and unknown reasons.
Review the GCC 13 CSES/Timus and GCC 14 Codeforces profiles, MinGW gthreads/ABI
cases, GCC 14–16 and every newly added matrix row.
After review, copy `out/library-review/library.json` **byte for byte** to
`data/library/library.json` and commit it in a follow-up change to the same PR,
recording the run URL. Keep `library-evidence.json` in the CI artifact for
review; never copy it into the baseline directory. This is the repository's
existing human import step; there is no automatic commit step. Rerun the PR and
require a clean comparison.
Do not manually alter generated fields to make a diff disappear.

Raw profile reports need not also be committed beside TOMLs. For a diagnostic
against a particular judge, extract its `builds["profile-<full-name>"]` object
from the reviewed **library-evidence.json** into a JSON file, or download its
raw `*-library.json` artifact. The compact baseline is not a raw probe report.
Generating source and checking submitted stdout do not invoke a local compiler:

```sh
python3 ci/library/probe.py <profile>-library.json --header charconv --output out/charconv-probe.cpp
# Submit out/charconv-probe.cpp under that judge option; save its stdout.
python3 ci/library/probe.py <profile>-library.json --header charconv --check out/judge-output.txt
```

Repeat with each header in an independent TU. The probe reports every name
known to that build's audit or header snapshots, including expected absences;
C++ cannot enumerate unknown new macro spellings. Missing headers are reported,
while a header that errors on the judge cannot supply runtime output. The checker
rejects incomplete/duplicate records and reports presence/value mismatches.
Observations never modify facts or the existing DPROBE protocol.

Capture logs print the audit duration as soon as it completes, followed by
discovery, input observations, preprocessing, serialization and total durations.
The QOJ container job times package setup, compiler acquisition, diagnostic
build, snapshot and diagnostic output separately.
[Run 44474492375](https://github.com/jcg9129/cpp-distill/actions/runs/44474492375/job/135391720777) spent 29m51s
in `Install container tools`, before checkout; the audit never ran. Bootstrap
APT network waits/retries are bounded and it avoids installing an extra default
GCC before the pinned GCC 15. The job timeout remains 30 minutes.

Run offline Python verification separately from the existing directories, whose
scripts use local module names:

```sh
timeout 300 python3 -m pytest -q ci/library
timeout 300 python3 -m pytest -q ci/profiles
timeout 300 python3 -m pytest -q ci/extensions
timeout 300 python3 -m pytest -q ci/drift
```

Install the pinned `ci/drift/requirements-test.txt` first. No C++ build or distill
execution is needed for these tests.

## Resolver embedding (part B)

CMake invokes `embed.py` with the reviewed baseline. It validates versions,
content addresses and references, then emits interned dense header-state arrays
with shared name/value pools, build identities and the global A12 member-name
union. Feature transitions cover standard-list headers. Values dependent on
volatile native inputs remain unknown, using conservative per-major dependency closures even
for own-build snapshots. Missing/malformed data fails configuration; edits to
the baseline or generator trigger regeneration. Runtime needs no data files.
The embedded provenance records schema 2, generator 3 or 4, embedding format 1
and the SHA-256 of the exact baseline bytes. Generator 4 also embeds the global
member-name union, independently of resolver profile selection, and reports
`library member inventory: worker headers + N scanned builds (generator 5)`.
Generator 3 remains accepted without an inventory and reports
`library member inventory: worker headers; scanned-build inventory unavailable`.
The committed data is intentionally left at generator 3 until the full CI
matrix is captured, reviewed and imported; do not regenerate it locally.
