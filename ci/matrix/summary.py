# SPDX-License-Identifier: GPL-3.0-or-later
"""Render all observations, including failed rows, outside container steps."""
import argparse
import csv
import json
import os
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("directory", type=Path, nargs="?", default=Path("results"))
args = parser.parse_args()
rows = []
for path in sorted(args.directory.rglob("rows.json")):
    rows.extend(json.loads(path.read_text(encoding="utf-8")))
fields = ["platform", "gcc", "dialect", "check", "result", "reason", "passed"]
with (args.directory / "compatibility.tsv").open("w", encoding="utf-8", newline="") as stream:
    writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t")
    writer.writeheader()
    writer.writerows(rows)

def cell(value):
    return str(value).replace("|", "\\|").replace("\n", "<br>").replace("\r", "")

text = "## Compatibility matrix\n\n| " + " | ".join(fields) + " |\n"
text += "| " + " | ".join("---" for _ in fields) + " |\n"
text += "".join("| " + " | ".join(cell(row[key]) for key in fields) + " |\n" for row in rows)
if not rows:
    text += "\nNo observations: inspect acquisition/build logs and failed workflow steps.\n"
(args.directory / "compatibility.md").write_text(text, encoding="utf-8")
print(text)
if os.environ.get("GITHUB_STEP_SUMMARY"):
    with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as stream:
        stream.write(text)
