# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import json
from common import PLATFORMS

parser = argparse.ArgumentParser()
parser.add_argument("--platforms", default="all")
parser.add_argument("--official", action="store_true")
args = parser.parse_args()
selected = [p["platform"] for p in PLATFORMS] if args.platforms == "all" else args.platforms.split(",")
unknown = set(selected) - {p["platform"] for p in PLATFORMS}
if unknown:
    parser.error(f"Unknown platforms: {sorted(unknown)}")
rows = [dict(p) for p in PLATFORMS if p["platform"] in selected and (not args.official or p["official"])]
if not rows:
    parser.error("No platforms selected")
for index, row in enumerate(rows):
    row["source_artifact"] = index == 0
    row["sdk_container"] = (dict(image=row["container"], volumes=[
        "/usr/local/lib/android:/opt/unused-android",
        "/usr/share/dotnet:/opt/unused-dotnet",
        "/opt/ghc:/opt/unused-ghc"]) if row["container"] else None)
print(json.dumps({"include": rows}))
