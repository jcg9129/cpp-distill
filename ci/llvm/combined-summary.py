# SPDX-License-Identifier: GPL-3.0-or-later
import json
from pathlib import Path
from common import summary, write_json

results = {}
for directory in Path("artifacts").glob("spike-*"):
    results[directory.name.removeprefix("spike-")] = {
        str(path.relative_to(directory)): json.loads(path.read_text(encoding="utf-8"))
        for path in directory.rglob("*.json")
    }
write_json("combined-summary.json", results)
summary("D1 distribution spike", results)
