# SPDX-License-Identifier: GPL-3.0-or-later
"""Textual A1 inventory: deliberately include inactive defines and undefines."""
import argparse
from datetime import date
from pathlib import Path
import re

VERSION = 1
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


def collect(text):
    # Phase 2 before comments, including GCC's whitespace-before-newline variant.
    text = re.sub(r"\\[ \t]*(?:\r\n|\n)", "", text)
    # Keep quotes intact so comment markers in strings cannot hide a directive.
    raw = r'(?:u8|u|U|L)?R"(?P<delimiter>[^\s()\\]{0,16})\(.*?\)(?P=delimiter)"'
    tokens = raw + '|' + r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|/\*.*?\*/|//[^\n]*'
    text = re.sub(tokens, lambda m: (" " if m[0].startswith("/") else m[0]), text, flags=re.S)
    return set(re.findall(r"^[ \t]*#[ \t]*(?:define|undef)[ \t]+([A-Za-z_][A-Za-z_0-9]*)", text, re.M))


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


def generate(library, directories, output, versions, exclude=(), standard_file=ROOT / "data/names/standard-macros.txt"):
    names, old_comments = read_list(output)
    standard_names, _ = read_list(standard_file)
    files = set()
    for directory in directories:
        directory = Path(directory)
        if not directory.is_dir():
            raise ValueError(f"include directory missing: {directory}")
        for file in directory.rglob("*"):
            if file.is_file() and not any(part in exclude for part in file.relative_to(directory).parts):
                files.add(file)
    if not files:
        raise ValueError("no header files found")
    for file in sorted(files):
        # Headers may contain comments in legacy encodings; ASCII identifiers survive.
        names.update(collect(file.read_bytes().decode("utf-8", errors="replace")))
    names = {n for n in names if not reserved(n) and not standard(n, standard_names)}
    provenance = {c for c in old_comments if c.startswith("# Scanned:")}
    provenance.add(f"# Scanned: {'; '.join(versions)}; headers: {', '.join(str(p) for p in directories)}; excluded directories: {', '.join(exclude) or 'none'}; {date.today().isoformat()}")
    header = ["# SPDX-License-Identifier: GPL-3.0-or-later", f"# A1 {library}; generator ci/extensions/generate.py v{VERSION}; date {date.today().isoformat()}", *sorted(provenance), "# Union across scans; inactive #define and #undef names included."]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(header + sorted(names)) + "\n", encoding="utf-8", newline="\n")
    return len(names)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", choices=LIBRARIES, required=True)
    parser.add_argument("--include", type=Path, action="append", required=True)
    parser.add_argument("--exclude-dir", action="append", default=[])
    parser.add_argument("--headers-version", action="append", required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    output = args.output or ROOT / "data/extensions" / f"{args.library}.txt"
    try:
        count = generate(args.library, args.include, output, args.headers_version, args.exclude_dir)
    except (OSError, ValueError) as error:
        parser.exit(1, f"extension generation failed: {error}\n")
    print(f"{args.library}: {count} names -> {output}")


if __name__ == "__main__":
    main()
