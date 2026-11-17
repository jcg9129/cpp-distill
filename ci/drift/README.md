<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Profile maintenance protocol

`drift.yml` runs evidence and availability every Monday, cold reproduction on
the first of each month, and selected claims/profiles on dispatch. Only its
report job can write issues. Every external action is pinned to a commit.
Python 3.12 and the standard library suffice for every `ci/drift` command.

`availability.py` enumerates **both** `snapshot_compiler` and `build` where
present. It fully streams exact MSYS2 package files, snapshot Ubuntu `.deb`s,
registry index + unique linux/amd64 child manifest + config and every layer
(including QOJ's pinned Ubuntu acquisition container),
Homebrew bottle blob, and AtCoder GNU source/recipe. It verifies every digest
before reporting `verified`; metadata reachability is insufficient. Identical
pins shared by profiles are downloaded once per run. No package is installed,
unpacked or executed. See [MSYS2 signing keys](msys2-keys.md) and
[Ubuntu snapshot evidence](../profiles/ubuntu-snapshots.md). AtCoder's tarball
SHA512 comes from [GCC's release checksums](https://gcc.gnu.org/ftp/gcc/releases/gcc-15.2.0/sha512.sum).
Homebrew reuses `homebrew.download`; WinLibs reuses `winlibs.download`. The latter
helper was absent when this framework was added: no shipped profile references
WinLibs, so its explicit URL-to-SHA256 lock is empty. Add a reviewed ZIP URL and
digest to `winlibs.LOCKS` before shipping such a recipe profile.

`reproduction.py` consumes `snapshot-<profile>/<profile>-predefined.txt` and
`-probe-expected.txt` downloaded from the callable `profiles.yml`. It compares
raw bytes. Missing profile/probe artifacts, any difference, and build failure
are failures. The cold-start input disables the AtCoder compiler cache; it
does not change the existing dispatch behavior.

## Evidence watch schema (version 1)

Files are `data/judges/watch/<full-profile-name>.toml`. There are no watch files
or production extractors in part 1. Missing watch files mean **no sources**,
not successful evidence, and create no evidence issues. A watch contains exactly
`schema = 1` and a nonempty `sources` array. Every source has exactly the
following fields (the example is synthetic):

```toml
schema = 1
[[sources]]
id = "language-list" # stable unique source ID within this profile
url = "https://judge.example/languages" # moving source, not a historical commit
kind = "deployment" # or "availability": listing, never evidence of adoption
extractor = "example" # ci/drift/extractors/example.py
excerpt = "Original-language excerpt retained for human review."
[sources.expected]
language_id = "42"
compiler_family = "gcc"
compiler_version = "15.2.0"
compile_arguments = ["-std=gnu++23", '-DNAME="exact text"', "-O2"]
runtime_notes = ["Linux amd64"]
library_notes = ["libstdc++"]
```

All three scalar facts are nonempty strings. All three array facts contain
strings; empty runtime/library arrays explicitly mean no recorded notes.
Argument order, whitespace, quoting and macro text are significant. No
normalization, token sorting or version-prefix matching occurs. Pinned commit
URLs remain historical sources in the profile; watches track moving deployment
URLs. `availability` sources remain labelled as listings in every report.

Each extractor exports `extract(payload: bytes, metadata: dict) -> dict` with
exactly the six fact fields above. Metadata contains final `url`, HTTP `status`,
`headers`, original `source_url`, and declared `kind`. Extractors parse bytes as
data; they never execute source text, interpolate it into shell commands or
fetch additional URLs. They must detect challenge/login content even at HTTP
200 and require exactly one complete matching entry. Errors are imported from
`evidence`:

| Result class | Meaning |
| --- | --- |
| `same facts` | All structured facts equal the watch expectations; the only pass. |
| `different facts` | Candidate drift; extracted structured facts differ. |
| `extractor failure` | `ExtractorFailure`, malformed facts, zero/multiple matches, missing fields, or unexpected extractor exceptions. |
| `fetch failure` | `FetchFailure`: login/challenge/non-source page despite HTTP 200. |
| `evidence unavailable` | HTTP errors (including 403/429), timeouts/transport errors or `EvidenceUnavailable`. |

Part 2 must test each extractor against saved original, changed, missing,
duplicated and challenge pages. This part tests the runner with synthetic
fixtures and a synthetic extractor defined only in tests.

## Result artifacts and durable issue state

Each job writes `<claim>.json` in artifact `drift-<claim>`. Envelopes contain
exactly `schema` (1), `claim`, full selected `profiles`, numeric-string `run_id`,
UTC ISO `timestamp`, ISO `[year, week]` `window`, boolean `complete`, and
`results`. Each row contains exactly `profile`, stable `source`, `host`, `kind`,
`status`, and bounded text `detail`. Availability results are `verified`,
`unavailable`, or `integrity alert`; reproduction results are `identical`,
`different`, `missing artifact`, or `build failure`. Reproduction's `kind` is
`availability` (it has no adoption claim or upstream host).

The reporter reconstructs expected source coverage from the checked-out locks
and watch files. It validates every selected artifact's schema, run identity,
profile selection, source coverage, hosts, kinds and result classes **before
any writes**. Missing, malformed or incomplete artifacts change no state for
any claim. A cancelled or skipped check job similarly blocks reporting. A
completed reproduction with failed/skipped individual profiles is different:
missing outputs are explicit reproduction failures. Fetched text is passed as
JSON through urllib, never evaluated by a shell. The `drift` label is created
when first needed.

One issue per profile/claim is titled `drift: <profile>: <claim>`. State lives
in its `<!-- distill-drift-state-v1 ... -->` JSON block, recording each source's
status, consecutive failures, consecutive unavailable weekly windows, last
success timestamp, last run ID, host, kind, detail and review deadline. Issue
state also records last full success and run ID. Initial passes and the first
three unavailable evidence windows create **closed tracking issues**, retaining
history without a premature open alert. Zero-source evidence creates none.
Do not delete tracking issues or their state blocks. Duplicate or corrupt
state stops the reporter for review.

Only status/source transitions generate comments; repeated failures still
update counters in the body. GitHub reruns with the same run ID and older runs
are ignored. Multiple dispatches within a week increment the unavailable-window
counter only once. Four adjacent ISO weekly windows of unavailability open the
profile's evidence issue as manual review, with a deadline 14 days later. Gaps
or a different result reset that sequence. Counters never advance `updated`.

Availability failures reopen on a new transition and auto-close only after
verified recovery. Evidence/reproduction issues never auto-close. Human closure
survives an identical repeated failure; a new failure transition reopens it.
`drift: upstream: <host>` aggregates transport failures affecting multiple
profiles. Its source map includes persisted unselected profiles, so checking
only one profile cannot recover the others. Availability-only aggregates close
after all their sources verify recovery; aggregates containing evidence remain
open for human review. No issue API is invoked by local tests.

Run `python3 -m unittest discover -s ci/drift` and
`python3 -m unittest discover -s ci/profiles`. Tests use local fixtures and
mock HTTP responses only.
