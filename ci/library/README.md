<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# L1c CI data pipeline

Design: [§4.3.3](../../docs/design.md#433-library-feature-test-macro-facts-l1c).
All compiler execution and real data generation happen in CI. Local tests use
synthetic text and mocked subprocesses only. Part A does not enable resolver facts.

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

The generated artifact contains two files (schema/generator 2):

- `library.json`: the compact, deterministic baseline to import into
  `data/library/`. It retains build metadata, per-header snapshot facts/status,
  per-major/root classifications and cross-build agreement. Sorted JSON has one
  record per line. A successful empty snapshot proves no feature macros;
  `missing`, `error` and `disabled` snapshots prove nothing.
- `library-evidence.json`: complete build reports, including installed-header
  definitions, lexical conditions, include edges/targets, request/value support,
  compiler definitions, per-root/per-value audit results and parse failures.
  Review this uploaded file; do not commit it. Each compact build contains
  `evidence_sha256`, the full SHA-256 of its complete report serialized as ASCII
  JSON with sorted keys and separators `(',', ':')`, without a trailing newline.
  Thus evidence-only changes still fail the byte comparison.

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

The two IEEE candidates are verified from their actual reachable definitions;
configuration switches never receive blanket trust. Header writes and
command/startup overrides revoke compiler-input trust for that root. Native-model
names remain untrusted under native flags. Literal include operands and
`__has_include`/`__has_include_next` arguments are not macro identifiers; the
availability predicates themselves remain untrusted because their presence tests
are not recorded as verified build inputs. A reachable computed include,
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
