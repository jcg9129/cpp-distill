# distill options

This page explains every command-line option: what it does, when to use it, how it
interacts with other options, and what the report says about it. The
[README](../README.md#everyday-usage) has the one-line summary table, and
[design.md](design.md) the full specification.

```sh
distill [distill options] <compiler> [compiler options] main.cpp
```

Distill options go **before** the compiler name; everything from the compiler
name on is the compile command you would type to build the solution. Without a
compiler name, `g++` from `PATH` is used (`distill main.cpp` means
`distill g++ main.cpp`). distill refuses a `g++` that is really Clang (the stock
macOS one) and asks for real GCC; a Clang named explicitly is accepted and noted.

- [Recipes](#recipes)
- [Output](#output): `-o`, `--report`, `-v`
- [Judge](#judge): `--judge`, `--judge-probe`, `--part`, `--judge-check`, `--record`
- [Pruning](#pruning): `--no-prune`, `--merge-only`, `--keep`, `--no-assume`
- [Size and layout](#size-and-layout): `--strip-comments`, `--no-line-markers`, `--compact-whitespace`, `--shorten-names`
- [Checking](#checking): `--verify-run`, `--strict`
- [Speed](#speed): `--verify`, `--jobs`
- [Information](#information): `--help`, `--version`
- [Exit codes](#exit-codes)

## Recipes

| Goal | Command |
| --- | --- |
| Submit to Codeforces, smallest readable file | `distill --judge codeforces --shorten-names -o submit.cpp g++ -std=c++23 -O2 -I ~/lib main.cpp` |
| Small file, original identifier names | `distill --judge codeforces --strip-comments --compact-whitespace -o submit.cpp g++ ... main.cpp` |
| Keep comments and line structure, only drop unused code | `distill --judge codeforces -o submit.cpp g++ ... main.cpp` |
| Only merge headers into one file, remove nothing | `distill --merge-only -o merged.cpp g++ ... main.cpp` |
| Also check the output on sample inputs | add `--verify-run samples/` |
| Find out why the result is not `pruned`, or what was kept and why | add `--report report.txt` |
| Fail a script when the result is not fully pruned and judge-checked | add `--strict` |

Pick the profile for the judge you submit to (`distill --judge list`). Without
`--judge`, distill knows nothing about the judge's compiler, and code with
target-dependent `#if`s can't be pruned.

How much each size option saves, measured on the 129 atl library examples (atl
ceaeac6, distill a810fae, judge cses, CI sweep 45866832372; all 129 pruned):

| Options | Total bytes | Over 64 KiB (Codeforces limit) |
| --- | ---: | ---: |
| none | 9,422,999 | 63 |
| `--strip-comments` | 4,582,951 | 20 |
| `--strip-comments --no-line-markers` | 4,012,810 | 15 |
| plus `--compact-whitespace` | 3,565,364 | 11 |
| `--shorten-names` (implies the other three) | 3,289,161 | 7 |

## Output

### `-o <file>`

Write the checked source to `<file>` instead of stdout. The path must differ
from the input file and from `--report`. Nothing is written if distill fails
(exit 1).

### `--report <file>`

Write a plain-text explanation of the run:
- the result line;
- the judge profile, and the assumptions the output relies on;
- how each compiler option was classified;
- every check with its compiler command line;
- every resolved, dead or unknown preprocessor conditional, with the facts used;
- the pruning analysis.

The pruning analysis has one `family` line per declaration (kept or removed,
the reason, and the declaration that caused it), one `member unit` line per
member function with its bytes, and the transformations the size options
applied.

Start here whenever the result is `flat` or `raw` instead of `pruned`: the
`pruning disabled:` and `... rejected:` lines say why. See
[understanding the result](../README.md#understanding-the-result) for the common
reasons and what to do about them, and [§4.9](design.md#49-report) for the full
format.

### `-v`

Print progress on stderr: which step is running (probes, baseline build,
resolution, pruning worker, each check).

## Judge

### `--judge <selector>`

Choose the judge profile. The selector is:
- a full name, such as `codeforces-gcc14-cpp23`;
- an alias, such as `codeforces`;
- a path to a TOML profile; any selector containing a path separator or ending
  in `.toml` is loaded as a file.

`--judge list` prints all profiles (bundled and user), with alias,
origin, reproduced/approximate status, last update and latest observation,
and needs no compile command.

The profile supplies the judge's compiler facts: its command, predefined macros,
target, and library configuration. distill uses them:
- to resolve conditionals the way the judge would;
- to choose the checking configuration;
- to report `target` verification when the local compiler reproduces the
  judge's preprocessing environment, and `local` otherwise.

Aliases follow the newest supported GCC for that judge and may move between
releases; full names never change meaning. Every run prints
`judge: <selector> -> <resolved name>` on stderr, and the report records the
profile digest. User profiles live in `~/.config/distill/judges` (or
`$XDG_CONFIG_HOME/distill/judges`, `%APPDATA%\distill\judges`). See
[Judges](../README.md#judges) and the [profile format](../data/judges/README.md).

### `--judge-probe <selector>` and `--part N/M`

Print a diagnostic C++ program for the profile. You submit it on the judge
(custom invocation or code test) with the profile's exact compiler option, and
save its stdout. Use the same distill version to generate and check.
`--part N/M` prints part N of M (default `1/1`). When the source or its output
exceeds the profile's budgets, increase M and submit every part `1/M` through
`M/M` separately. It takes no compile command and no other distillation options.

### `--judge-check <selector> <output-file>...` and `--record <dir>`

Compare saved probe outputs against the profile's expected facts. It reports
matches, mismatches, missing, extra and unsupported facts, and exits 0 only when
nothing failed. A pass is a user-reported match for that test and date, not proof
of the judge's environment. `--record <dir>` also saves dated evidence for the
observation. Profiles last reviewed more than 180 days ago print a one-line note
suggesting this check. See
[checking a judge yourself](../README.md#checking-a-judge-yourself).

## Pruning

By default distill inlines your local headers, resolves the preprocessor
for the judge, and removes code the program can't use on the judge. The result
is `pruned`. If something prevents a sound analysis, it falls back to `flat`
(inlined and resolved), or `raw` (inlined only). Every candidate is built
with the checking configuration before it is written.

### `--no-prune`

Keep every declaration, but still resolve preprocessor conditionals for the
judge and remove inactive `#if` branches (result `flat`, or `raw` if resolution
or its checks fail). Use `--merge-only` to keep everything. The report says
`pruning disabled: --no-prune` when pruning is skipped.

### `--merge-only`

Inline local headers only, keeping every preprocessor conditional and every
line of code. The result is `raw`: resolution, pruning and later renderings
do not run. Its bytes match the normal raw fallback for the same input and
comment-stripping option.

The original still builds as the baseline. The exact raw bytes pass the usual
preprocessing comparison against the original and local-command build checks;
`--verify-run` compares samples against the baseline. `--judge` is allowed and
keeps the usual raw check configuration and verification level.

Cannot be combined with `--no-line-markers`, `--compact-whitespace`,
`--shorten-names`, `--keep`, `--strict` or `--no-assume` (usage error, exit 1).
`--no-prune` is accepted as redundant. `--strip-comments` is allowed: raw already
strips comments before its checks, retaining comments needed for directive/line
safety. The result line says `raw`; the report and stderr say
`resolution disabled: --merge-only`.

### `--keep <name>`

Keep a declaration that pruning would remove, for example a debugging helper
you call from a debugger or want in the submission. `<name>` is an indexed
(namespace-scope) name; repeat the option for more. The same can be written in
the source:

```cpp
// distill: keep
void debug_state() { /* ... */ }
```

An unknown name is an error. `--keep` needs a checked flat configuration.

### `--no-assume=<name>`

Turn off one assumption or policy that pruning or shortening relies on, while
keeping the rest of the analysis. Repeat the option, or give a comma list. Each
assumption is something distill can't prove from your machine about the judge's
library or compiler; turning one off makes the output larger but depends on
less. The report lists every assumption an output relied on.

| Name | Id | What it lets distill do | With `--no-assume` |
| --- | --- | --- | --- |
| `instantiation-effects` | A11 | Remove code whose only effect would be through friend-injection instantiation, the destruction of template parameter objects or constexpr temporaries, or block-scope statics never reached. | That code is kept. |
| `library-spelled-members` | A12 | Remove unused member functions of a kept class, assuming the library calls user members only under names its headers spell (or operators, constructors, destructors). | Classes are kept whole; whole-declaration pruning remains. |
| `library-macro-values` | A9 | Use the recorded values of standard object-like macros, as well as `__cpp_lib_*`, to resolve conditionals. | Only `__cpp_lib_*` values are used; conditionals on other library macros may stay unresolved. |
| `library-spelled-names` | A15 | Shorten identifiers the judge's library can't observe (`--shorten-names`). | Names stay unchanged; the compact layout still applies. |
| `unspelled-functions` | D3f | Remove free functions whose name the kept code never spells, even if their signature mentions a kept type. | Such functions are kept if their signature names a kept type. |
| `idempotent-standard-headers` | policy | Drop a repeated top-level `#include` of a C++ standard header when the analysis proves the earlier one already provided it. | Repeated includes are kept. |
| `unobserved-guard-macros` | A14 | Drop distill's own unused `DISTILL_ONCE_<hash>` include-guard defines. | The guard defines stay. |

The always-on assumptions (A1–A7) and the exact wording of each are in
[design.md §1.3](design.md#13-trust-assumptions-user-amended-through-2026-11-18).

## Size and layout

These options only change how a checked result is rendered. Each one checks
that the change can't alter what the program does, and otherwise leaves the
output unchanged and says why in the report. None of them renames anything
except `--shorten-names`, and none produces one-line or obfuscated code:
Codeforces forbids obfuscation, and the output stays readable.

### `--strip-comments`

Remove comments. A comment that a directive or line splice needs (for
example inside a macro definition's continuation) may remain. `// distill: keep`
annotations are read before stripping.

### `--no-line-markers`

Omit the `#line` markers distill writes so that diagnostics point at your
original files. They are omitted only from a `pruned` result, and only when
the analysis proves no kept code can observe source positions:
- `__LINE__`, `__FILE__`, `__builtin_LINE`;
- `assert` without `NDEBUG`;
- call-site default arguments, such as `std::source_location::current()`.

Otherwise the markers stay and the report says `line markers kept: <what> at <file>:<line>`.

### `--compact-whitespace`

Trim leading and trailing horizontal whitespace and remove empty lines. It
implies `--no-line-markers` and needs the same proof, extended to columns. The
contents of string literals, raw strings and spliced logical lines are kept
byte for byte. If refused, the output is unchanged and the report says
`whitespace kept: <why>`.

### `--shorten-names`

Shorten identifiers to readable abbreviations from a dictionary of common
programming words (`number` → `num`, `position` → `pos`, …). This is not
`a`, `b`, `c` renaming, so the submission stays readable. It implies
`--strip-comments --no-line-markers --compact-whitespace`, and applies only to a
checked `pruned` result.

These names are never changed:
- names the judge's library could observe (assumption A15);
- names found by argument-dependent lookup;
- names the compiler or the language protocol needs (for example `main` and
  the coroutine names);
- macros and their parameters;
- names used in stringizing or token pasting;
- reserved names.

Words with digits and names of up to four characters stay, and a name is
abbreviated only if that saves at least two characters.

The whole shortening is refused, and the report says why, when the program can
observe its own identifiers: RTTI names, `std::source_location`,
`__builtin_dump_struct`, or a conditional that survives in the output. The
shortened program must re-lex to the same token structure and pass the same
checks as the unshortened one; otherwise distill keeps the unshortened output.
`--no-assume=library-spelled-names` keeps names and applies only the layout
options.

## Checking

Every written artifact is built with the checking configuration. These options
add more checks.

### `--verify-run <path>`

Also run the programs on sample inputs and compare their stdout byte for byte,
and their exit status. Comparisons are original against raw, original against
flat, then flat against pruned. `<path>` is one input file (any extension), or a
directory whose immediate `*.in` files are used in sorted order; the directory
must not be empty. Repeat for more inputs. There are no expected outputs, and
stderr is not compared. A failed comparison rejects that artifact, and distill
keeps the last one that passed.

Samples don't prove the programs equivalent, and nondeterministic programs can
fail. When the checking compiler builds for another target, the runs are
skipped and the report says `skipped: can't run target binaries here`.

### `--strict`

Exit with status 3 (the source is still written) unless the result is `pruned`
with `target` verification. Use it in scripts and editor integrations that should
notice a fallback.

## Speed

A run takes several seconds to half a minute, mostly in the C++ compiler: distill
builds the original program, the raw file, and every artifact and rendering it
might emit, each with the judge's flags, and preprocesses most of them twice for
comparison. These options make a run faster. They don't change what distill
checks about the file it writes. The defaults keep the full sequence.

Measured with `--shorten-names` (which implies the other size options), judge
cses, on a 6-core arm64 machine, one run each; the written file was
byte-identical in every column:

| Example | default | `--verify=final` | `--jobs=4` | both |
| --- | ---: | ---: | ---: | ---: |
| atl segment-tree quick-start (6.4 KB out) | 10.6 s | 7.0 s | 9.0 s | 6.6 s |
| atl top-tree basic (73 KB out) | 21.1 s | 15.7 s | 18.7 s | 14.6 s |

`--jobs` helps less than the number of cores suggests, because most builds
depend on the result of the one before.

### `--verify=all|final`

With the default `all`, every rendering stage is built and checked. With
`final`:
- the raw, flat and pruned artifacts are checked exactly as with `all`,
  including comment stripping, which is part of those artifacts;
- line-marker removal and whitespace compaction skip their build, run and
  preprocessing comparison when another requested rendering follows them
  (for example `--shorten-names`). Their source-position proofs and token
  invariants still run;
- the last requested rendering gets the full check. If it fails or is refused,
  distill checks the skipped renderings in reverse order until one passes, and
  otherwise writes the checked pruned artifact.

Every written file has passed a full judge-flag build and all checks of its
stage. When all stages pass, the output is byte-identical to `all`.

The one accepted difference: if an intermediate rendering would fail but a
later one passes, `final` writes the later, checked rendering, where `all` would
stop at an earlier one.

The report says `option verify=final`, with one line per skipped stage:
`verify: <stage> build skipped (--verify=final)`, where the stage is `unmarked`
or `compact`. It works with `--verify-run`.

### `--jobs=N`, `-j N`

Run independent compiler and preprocessor invocations concurrently, at most N
at a time (default 1). Output, report contents and order, exit status and
diagnostics are the same as with one job; only the wall time changes. Commands
with compiler flags distill doesn't model stay serial, because such flags can
share files or load plugins. Each parallel job is a full compiler, so limit N to
what the machine's memory allows.

## Information

### `--help`

Print the option summary.

### `--version`

Print the distill version, the source commit and the LLVM revision it was built
with. Include this line in bug reports.

## Exit codes

| Code | Meaning |
| --- | --- |
| 0 | Checked source was written, or the diagnostic command succeeded. |
| 1 | No source written. Reasons include invalid or unsupported input, a failed build of the original program, no checked artifact, or a failed diagnostic command. |
| 3 | Source was written, but `--strict` did not get `pruned` with `target` verification. |
