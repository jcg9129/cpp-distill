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

The merged `library.json` (schema/generator 1) has:

- `builds`: original metadata, per-header `status` (`ok`, `missing`, `error`,
  `disabled`) and `macros`, plus the actual textual audit evidence. A successful
  empty map means no feature macros. Error/missing/disabled maps prove nothing.
- `versions`: per GCC major, independent names with permitted integer values,
  dependent names with unrecognised/configuration dependencies, and build IDs.
- `agreement`: per GCC major/header, `defined` values, proven `absent` names,
  and explicit `unknown` reasons. Different profile dialects and flags remain
  part of the all-build agreement; this can conservatively reduce coverage.

The audit retains actual lexical conditions, including inactive definitions,
previous elif conditions, internal feature-request/value definitions and include
edge dependencies. It verifies the embedded IEEE candidate macros against each
installed build; unknown predicates or replacements fail closed. Header writes
and command/startup overrides revoke compiler-predefine trust. Audits retain
the trusted compiler definitions so changes to dependency replacements are visible. Native-model names are not trusted inputs under
native flags. Unresolved computed includes taint the audit; the existing A1
reachability limitation is not silently taken as proof of absence. The merger
requires all audited definitions for an absence fact to be independent.
Files with unparseable conditional structure or macro writes are retained in
`audit.unparsed` with their reasons. An unparsed file taints all feature results
with `<unparsed-file:PATH>` if another audited file includes it or its text
contains `__cpp_lib_` or `__glibcxx_`; otherwise it is listed without taint.
Per-header preprocessing snapshots retain their independent `ok`/`error` status.

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

Review the failed check's full diff and the JSON's per-build status, independent
pairs, dependent names/dependencies and unknown reasons. Review both GCC 13
builds, MinGW gthreads/ABI cases, GCC 14–16 and every newly added matrix row.
After review, copy `out/library-review/library.json` **byte for byte** to
`data/library/library.json` and commit it in a follow-up change to the same PR,
recording the run URL. This is the repository's existing human import step;
there is no automatic commit step. Rerun the PR and require a clean comparison.
Do not manually alter generated fields to make a diff disappear.

Raw profile reports need not also be committed beside TOMLs: the combined
baseline retains them. For a diagnostic against a particular judge, extract its
`builds["profile-<full-name>"]` object from the reviewed baseline into a JSON
file, or download its raw `*-library.json` artifact. Generating source and
checking submitted stdout do not invoke a local compiler:

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
