<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# User measurement corpus

Private solutions and libraries are never committed. Keep the corpus in any
directory outside the repository, with one directory per solution containing
`main.cpp` and an optional `cmd.txt` holding the ordinary compile command.

From the repository, point the local measurement script at one or more corpus
directories:

```sh
ci/measure-local.sh --jobs 2 /path/to/solutions /path/to/more-solutions
```

This requires Python 3, CMake, Ninja, a C/C++ compiler (GCC on Linux, Clang on
macOS), and GNU `timeout` (Homebrew `coreutils` provides `gtimeout` on macOS).
The solution compile commands must also name an installed compiler.
The script acquires the pinned release SDK into the ignored `spike-work/sdk/`
and reuses an existing unpacked SDK there. It retains that SDK between runs;
the acquisition helper removes its compressed download after extraction.
The Release build uses two jobs by default and is removed on exit; pass
`--keep-build` to retain the ignored `build-measure/` directory for reuse.

Results go to `measurements/local/user.md`, `user.tsv` and `user.log` in the
ignored `measurements/local/` directory. Only the generated tables are shared;
review them for private paths or identifiers before sharing. Keep solutions,
libraries and diagnostic logs private. This directory is an ignored fallback
for local sources, with only this README tracked; other directories inside
the repository are refused by the script.
