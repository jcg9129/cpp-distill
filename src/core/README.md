# Core library (D2a)

`distill_core` is a C++20 static library with no LLVM or third-party dependency.
`command/` expands GCC response files and classifies a compile invocation,
retaining argument spelling and order. Its queries expose include paths,
local macros, dialect and reasons that transformations must be disabled.
`buildArgv` replaces source/output paths, drops dependency outputs, optionally
keeps local macros, and appends defines (bare names or `NAME=value`). Its
`generatedFile` parameter defaults to true, dropping `-I`/`-iquote`; pass false
for baseline builds. Paths in the command retain their original spelling and
are interpreted relative to the command's absolute `cwd`.

Response files use whitespace, quotes and backslash escapes, with at most
16 levels of nesting. All relative response filenames use the command cwd,
including nested files. Missing files, cycles, unmatched quotes and dangling
escapes are errors. Unlike GCC, a missing response file is **not** passed
through as a literal argument: rejecting it keeps classification reliable.

`process/` resolves executables explicitly through PATH and invokes them
without a shell. POSIX uses `posix_spawn` when `posix_spawn_file_actions_addchdir_np`
is available, otherwise `fork`/`execve` (including on glibc 2.28). The fallback
prepares arguments, environment and paths before forking, uses only
async-signal-safe calls in the child, and reports setup/exec failures through
a CLOEXEC pipe. Both paths use a new process group, CLOEXEC pipes and concurrent
`poll` draining. Windows uses UTF-16 `CreateProcessW`, explicit
CRT quoting, an inherited-handle list, two reader threads and a Job Object
assigned before the suspended process resumes. Output limits apply separately
to stdout and stderr; excess output is drained and discarded. The deadline
includes waiting for descendants holding pipes open; after the direct child
exits, they get at most two seconds to finish before group/job termination.
The remaining group/job is also terminated when a successful run returns.
POSIX stops signalling a group once it has been killed or is known empty.
After job termination, Windows gives readers up to one second to finish,
then cancels blocked reads and marks the affected output as truncated.
Linux applies `RLIMIT_AS` with `prlimit` immediately after spawn, with a small
race before the limit takes effect. macOS ignores the memory-limit field
because it has no equivalent `prlimit`; Windows limits process memory in
the job. No signal handler or global process registry is installed.

`probe/` provides pure parsers for verbose include search lists and `-dM`
macros, identifies GCC/Clang/Apple Clang, infers the effective dialect, and
drives three bounded compiler invocations. Search entries retain order, kind,
framework status and a GCC-internal flag; none are removed. Macro replacement
text retains every byte after the single separating whitespace character.
Probes forward modelled flags except user include paths, optionally replace
local macros, and pass only `-m32`/`-m64` to `-dumpmachine`. Each invocation
has a 30-second deadline and 4 MiB per-stream cap. Unique temporary directories
default to `std::filesystem::temp_directory_path()` (or `ProbeOptions::temporaryRoot`)
and are removed on success and failure. Operational entry points return `Result<T>` with an
error message; access `value()` only after `ok()`.

Build and test locally (at most two build workers):

```sh
cmake -S . -B build -G Ninja && cmake --build build -j2 && timeout 120 build/tests/unit/distill_unit_tests
```

Use `--filter substring` to select tests. `ctest --test-dir build
--output-on-failure --timeout 120` is the portable equivalent used in CI.
The helper executable supplies process-test commands on every platform.
Configure with `-DDISTILL_FORCE_FORK_SPAWN=ON` to exercise the POSIX fallback;
Linux CI runs the whole suite in both modes.
Fixtures are checked out byte-exact via `.gitattributes` and read in binary mode.
`tests/unit/fixtures/probe/gcc15-*.txt` were captured with local Ubuntu
g++ 15.2.0 on AArch64 (`-xc++ -E -v`, `-xc++ -dM -E`, `-dumpmachine`) on 2026-11-15;
files with `synthetic` in their names model other hosts. The real probe test
uses `g++` from PATH and skips if it is absent (on macOS that name may identify
Apple Clang). `spike/` remains an independent project.
