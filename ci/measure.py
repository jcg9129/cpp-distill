# SPDX-License-Identifier: GPL-3.0-or-later
"""SDK measurement driver: one bounded runner invocation per solution."""
import argparse
import csv
from pathlib import Path
import subprocess
import sys

COLUMNS = ["file", "result", "raw_bytes", "flat_bytes", "pruned_bytes",
           "kept_bytes_primary", "pruning_disable_reasons", "diagnostic"]


def solutions(root):
    # Full sources use actual chrono/format APIs; extraction's old-library
    # interface models are deliberately excluded from measurements.
    for case in sorted(root.iterdir()):
        if not case.is_dir():
            continue
        expect = case / "expect.txt"
        if expect.exists():
            text = expect.read_text()
            if root.name == "prune" and any(line.startswith("disabled") for line in text.splitlines()):
                continue
            if root.name == "pipeline" and ("prune=yes" not in text or
                    any(line.startswith(("fault=", "checker=")) for line in text.splitlines())):
                continue
        for name in ("full.cpp", "main.cpp", "1.cpp"):
            source = case / name
            if source.exists():
                yield source
                break


def markdown(rows):
    def cell(text):
        return text.replace("&", "&amp;").replace("<", "&lt;").replace("|", "&#124;").replace("\n", " ")
    columns = COLUMNS[:-1]
    lines = ["<!-- SPDX-License-Identifier: GPL-3.0-or-later -->",
             "# Pruning measurements", "",
             "Byte counts describe checked raw/flat bytes and the attempted pruned candidate; `-` means that stage was not reached.",
             "Primary attribution counts each family byte once; structural bytes are separate. Analysis can belong to a rejected candidate.",
             "Error/raw/flat rows are observations, not expected-output assertions. Full diagnostics are in the TSV and per-file log.", "",
             "| " + " | ".join(columns) + " |", "| " + " | ".join(["---"] * len(columns)) + " |"]
    lines += ["| " + " | ".join(cell(row[key]) for key in columns) + " |" for row in rows]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", type=Path, required=True)
    parser.add_argument("--worker", type=Path, required=True)
    parser.add_argument("--root", type=Path, action="append", required=True)
    parser.add_argument("--output", type=Path, required=True, help="table basename")
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    rows, failed = [], False
    sources = [source for root in args.root for source in solutions(root)]
    if not sources:
        parser.error("no solutions found")
    row_file = args.output.with_suffix(".row.tsv")
    try:
        with args.output.with_suffix(".log").open("w") as log:
            for source in sources:
                print(f"MEASURE {source}", flush=True)
                row_file.unlink(missing_ok=True)
                command = ["timeout", "--kill-after=5", "120", str(args.runner.resolve()), "--measure",
                           str(args.worker.resolve()), str(source.resolve()), str(row_file.resolve())]
                log.write(f"\nFILE {source}\n"); log.flush()
                try:
                    result = subprocess.run(command, stdout=log, stderr=log, timeout=130, check=False)
                    returncode = result.returncode
                except (subprocess.TimeoutExpired, OSError) as error:
                    log.write(f"runner failure: {error}\n")
                    returncode = -1
                failed |= returncode != 0
                if row_file.exists():
                    with row_file.open(newline="") as stream:
                        actual = list(csv.DictReader(stream, delimiter="\t"))
                    if len(actual) == 1 and set(actual[0]) == set(COLUMNS):
                        rows.extend(actual)
                        continue
                failed = True
                rows.append(dict(zip(COLUMNS, [str(source), "runner-error", "-", "-", "-", "", "",
                                               f"runner exit {returncode}; see log"])))
    finally:
        row_file.unlink(missing_ok=True)
        with args.output.with_suffix(".tsv").open("w", newline="") as stream:
            writer = csv.DictWriter(stream, COLUMNS, delimiter="\t")
            writer.writeheader(); writer.writerows(rows)
        args.output.with_suffix(".md").write_text(markdown(rows))
    print(f"{len(rows)} measurements written to {args.output}")
    return int(failed)


if __name__ == "__main__":
    sys.exit(main())
