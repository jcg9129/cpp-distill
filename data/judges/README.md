<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Judge profiles

A profile is `<name>.toml`, where `name` contains only lowercase letters,
digits and hyphens and matches the filename. Use the flat TOML subset documented
in `src/core/README.md`; the shipped files are examples. `sources` is a required,
non-empty array of judge-published HTTP(S) URLs. Research the actual language
entry, compiler version, target, dialect and **all** command-line defines;
record the command in comments and update `updated` to the research date.
Mark inferred or unchecked facts with `# UNVERIFIED:` and explain them in the PR.
Keep preinstalled non-standard libraries in the description for now.

`predefined` names a complete compiler-default macro snapshot beside the profile.
Generate it with `python3 ci/profiles/snapshot.py data/judges/<name>.toml
--compiler <matching-g++>`, or dispatch `.github/workflows/profiles.yml` with
`all` or comma-separated names. The script checks the exact target spelling and
version (patch too when specified), passes only `-x c++ -std=... -dM -E`, and
records the compiler path, version banner and date. Judge defines remain in the
profile. Never substitute the local compiler or hand-edit a real snapshot.
The loader strips full-line `# ` or `//` metadata comments before D2a `parseMacros`.

The initial shipped snapshots are intentionally **absent**: loading either
profile reports `snapshot missing` until the matching compiler artifact has been
reviewed and checked in. An unavailable historical MSYS2 package fails the
workflow explicitly; a repository containing that exact version is needed.
To deliberately write an allow-list-only profile, omit `predefined` and use
`allowed = ["__GNUC__=14", ...]`. Unlisted facts then remain unknown. `allowed`
is forbidden alongside `predefined`; missing snapshots never trigger a fallback.

Review a snapshot together with its researched profile and A1 lists in one PR.
The Linux C++ library selection is `libstdc++`, whose protection checks also use
the embedded `glibc` list. MinGW's list includes both CRT and GCC C++ headers.
The macOS list currently covers SDK C headers only, as requested; a future
macOS GCC profile needs a deliberate C++ inventory composition decision.

The current AtCoder command uses `-O2`, `-march=native`, `-pthread`, `-fopenmp`,
`-fmodules` and other flags beyond the snapshot recipe. These can change compiler
macros. The official GCC Docker image also differs from AtCoder's build and host.
D2c follows the requested default-only recipe; these captures must not be treated
as a proven exact judge inventory without reviewing those differences. D2's
configuration/resolver work needs to close that design gap before relying on A4.
GCC also omits special builtins (`__LINE__`, `__FILE__`, `__has_include`, etc.)
from `-dM`; the resolver must model those separately. A complete `-dM` capture
alone does not prove the absence of every other compiler-defined name.
The exact Codeforces patch level and both exact triple spellings are inferred
and marked in the files. Snapshots do not establish those judge facts by themselves.

For updates, recheck the sources, change the profile and its date, regenerate
snapshots and extension inventories, review the macro diff and submit a PR with
the source evidence and compiler provenance. Artifacts never modify the repo
automatically. Profile lookup prefers an explicitly supplied user directory (or
an injected XDG/HOME/APPDATA environment) over embedded profiles; a path selector
loads that file directly. An invalid user override is an error, not a fallback.
