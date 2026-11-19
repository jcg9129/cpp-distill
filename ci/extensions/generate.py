# SPDX-License-Identifier: GPL-3.0-or-later
"""Textual A1 inventory: deliberately include inactive defines and undefines."""
import argparse
from datetime import date
from pathlib import Path
import re

VERSION = 2
LIBRARIES = ("glibc", "libstdc++", "mingw-w64", "macos-libc")
ROOT = Path(__file__).resolve().parents[2]
IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z_0-9]*\Z")


def reserved(name):
    return "__" in name or re.match(r"_[A-Z]", name) is not None


def standard(name, names):
    if name in names:
        return True
    return bool(re.fullmatch(r"(?:U?INT(?:_LEAST|_FAST)?)[0-9]+_(?:MIN|MAX|WIDTH|C)", name)
                or re.fullmatch(r"(?:PRI|SCN)[diouxXbB](?:LEAST|FAST)?[0-9]+", name))


def directive_text(text, mask_literals=False):
    # Phase 2 before comments, including GCC's whitespace-before-newline variant.
    text = re.sub(r"\\[ \t]*(?:\r\n|\n)", "", text)
    # Keep quotes intact so comment markers in strings cannot hide a directive.
    raw = r'(?:u8|u|U|L)?R"(?P<delimiter>[^\s()\\]{0,16})\(.*?\)(?P=delimiter)"'
    tokens = raw + '|' + r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|/\*.*?\*/|//[^\n]*'
    text = re.sub(tokens, lambda m: (" " if mask_literals or m[0].startswith("/") else m[0]), text, flags=re.S)
    return text


def collect(text):
    return set(re.findall(r"^[ \t]*#[ \t]*(?:define|undef)[ \t]+([A-Za-z_][A-Za-z_0-9]*)", directive_text(text), re.M))


def includes(text):
    # Literal operands only; computed include operands cannot be followed.
    return [(kind, quote == '"', name) for kind, quote, name in re.findall(
        r'^\s*#\s*(include_next|include)\s*([<"])([^>"\n]+)[>"]', directive_text(text), re.M)]


def reachable(search, standard_file=ROOT / "data/names/standard-headers.txt", *, with_spellings=False):
    """Follow all textual includes, active or not, including include_next.

    By default return A1's set of resolved files. With with_spellings, return
    an audit-key -> resolved-file mapping, retaining each search index and
    unresolved relative spelling, including aliases through symlinked dirs.
    """
    search = [Path(p).resolve() for p in search]
    queue, seen, reached = [], set(), {} if with_spellings else set()
    def location(path, index):
        if with_spellings and not path.is_relative_to(search[index]):
            # An absolute quoted operand can name another search directory.
            index = next((i for i, directory in enumerate(search)
                          if path.is_relative_to(directory)), None)
            if index is None:
                raise ValueError('reachable header outside compiler search paths: ' + str(path))
        return (path if with_spellings else path.resolve(), index)
    def resolve(name, begin=0, local=None, inherited=-1):
        if local is not None and (local / name).is_file():
            return location(local / name, inherited)
        for i in range(begin, len(search)):
            if (search[i] / name).is_file():
                return location(search[i] / name, i)
        return None
    for name in (s.strip() for s in standard_file.read_text().splitlines() if s.strip() and not s.startswith('#')):
        found = resolve(name)
        if found:
            queue.append(found)
    for i, directory in enumerate(search):
        pb = directory / 'ext/pb_ds'
        if pb.is_dir():
            queue.extend(location(p, i) for p in pb.rglob('*') if p.is_file())
    while queue:
        file, index = queue.pop()
        if (file, index) in seen:
            continue
        seen.add((file, index))
        resolved = file.resolve() if with_spellings else file
        if with_spellings:
            reached[f'{index}/' + file.relative_to(search[index]).as_posix()] = resolved
        else:
            reached.add(file)
        for kind, quoted, name in includes(resolved.read_bytes().decode('utf-8', errors='replace')):
            found = resolve(name, index + 1 if kind == 'include_next' else 0,
                            file.parent if quoted and kind == 'include' else None, index)
            if found:
                queue.append(found)
    return reached


def read_list(path):
    if not path.exists():
        return set(), []
    names, comments = set(), []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line.startswith("#"):
            comments.append(line)
        elif line:
            if not IDENTIFIER.fullmatch(line):
                raise ValueError(f"{path}: invalid macro name {line!r}")
            names.add(line)
    return names, comments


def generate(library, files, output, versions, standard_file=ROOT / "data/names/standard-macros.txt"):
    # Only files reached by reachable() are eligible. Never union an old broad scan.
    names = set()
    standard_names, _ = read_list(standard_file)
    for file in sorted(files):
        if not file.is_file():
            raise ValueError(f"reached header missing: {file}")
        names.update(collect(file.read_bytes().decode("utf-8", errors="replace")))
    names = {n for n in names if not reserved(n) and not standard(n, standard_names)}
    header = ["# SPDX-License-Identifier: GPL-3.0-or-later",
              f"# A1 {library}; generator ci/extensions/generate.py v{VERSION}; date {date.today().isoformat()}",
              f"# Scanned: {'; '.join(versions)}; {len(files)} reachable headers",
              "# Roots: data/names/standard-headers.txt and every ext/pb_ds/ header.",
              "# Transitive textual #include/#include_next, active or inactive; compiler search order.",
              "# Limitation: computed include operands are not followed.",
              "# Inactive #define and #undef names included; unions only across v2 reachability scans."]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(header + sorted(names)) + "\n", encoding="utf-8", newline="\n")
    return len(names)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", choices=LIBRARIES, required=True)
    parser.add_argument("--include", type=Path, action="append", required=True)
    parser.add_argument("--headers-version", action="append", required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    output = args.output or ROOT / "data/extensions" / f"{args.library}.txt"
    try:
        count = generate(args.library, reachable(args.include), output, args.headers_version)
    except (OSError, ValueError) as error:
        parser.exit(1, f"extension generation failed: {error}\n")
    print(f"{args.library}: {count} names -> {output}")


if __name__ == "__main__":
    main()
