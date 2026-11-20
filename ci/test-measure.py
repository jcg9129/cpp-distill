# SPDX-License-Identifier: GPL-3.0-or-later
"""Cheap checks for measurement selection, provenance and table formatting."""
import hashlib
import csv
import contextlib
import io
import subprocess
import sys
import tempfile
from unittest.mock import patch
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("measure", Path(__file__).with_name("measure.py"))
measure = importlib.util.module_from_spec(spec)
spec.loader.exec_module(measure)


class MeasurementTests(unittest.TestCase):
    def test_all_caide_cases_once_and_reviewed_hashes(self):
        root = Path("tests/corpus/caide")
        sources = list(measure.solutions(root))
        self.assertEqual(len(sources), 81)
        self.assertEqual(len({s.parent for s in sources}), 81)
        for line in (root / "SHA256SUMS").read_text().splitlines():
            digest, file = line.split("  ", 1)
            self.assertEqual(hashlib.sha256((root / file).read_bytes()).hexdigest(), digest, file)

    def test_caide_expectation_table_covers_the_adapter_cases(self):
        root = Path("tests/corpus/caide")
        rows = [line.split(" | ") for line in (root / "README.md").read_text().splitlines()
                if line.startswith("| `")]
        names = [row[0].removeprefix("| `").removesuffix("`") for row in rows]
        self.assertEqual(sorted(names), sorted(source.parent.name for source in measure.solutions(root)))
        self.assertTrue(all(row[1] in {"pruned", "flat-with-reason", "error-with-reason", "raw-with-reason"}
                            and row[2].strip(" |") for row in rows))

    def test_caide_port_has_individual_assertions_and_review_entries(self):
        root = Path("tests/corpus/caide")
        names = {source.parent.name for source in measure.solutions(root)}
        reviews = [line for line in (root / "PORT.md").read_text().splitlines() if line.startswith('- `')]
        self.assertEqual(len(reviews), 81)
        self.assertEqual({line.split('`')[1] for line in reviews}, names)
        for line in reviews:
            self.assertTrue(any(kind in line for kind in ('same removal:', 'keeps more because', 'unsupported because')), line)
        for name in names:
            lines = [line for line in (root / name / "expect.txt").read_text().splitlines()
                     if line and not line.startswith('#')]
            fields = dict(line.split('=', 1) for line in lines)
            self.assertEqual(len(fields), len(lines), name)
            self.assertIn(fields['exit'], {'0', '1'}, name)
            if fields['exit'] == '1':
                self.assertTrue(fields['reason'], name)
            else:
                self.assertIn(fields['artifact'], {'raw', 'flat', 'pruned'}, name)
                self.assertTrue(fields['contains'], name)
            for key, value in fields.items():
                self.assertTrue(key in {'exit', 'artifact', 'reason', 'prune', 'keep', 'strip', 'runtime'} or
                                key.split('.')[0] in {'contains', 'absent', 'sample', 'report', 'baseline-feature'}, (name, key))
                if key.startswith('sample.'):
                    self.assertTrue((root / name / value).is_file(), (name, value))

    def test_adversarial_selection_avoids_faults_and_refusals(self):
        sources = list(measure.solutions(Path("tests/cases/prune")))
        sources += list(measure.solutions(Path("tests/cases/pipeline")))
        self.assertTrue(any(s.parent.name == "structured_get" for s in sources))
        self.assertFalse(any(s.parent.name.startswith("checker_") for s in sources))
        self.assertFalse(any(s.parent.name == "pb_ds" for s in sources))
        self.assertFalse(any(s.parent.name == "pruned_bad_build" for s in sources))

    def test_driver_continues_after_timeout_and_keeps_rows(self):
        checks = Path(".checks"); checks.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=checks) as directory:
            root = Path(directory)
            cases = root / "solutions"; cases.mkdir()
            for name in ("a", "b"):
                case = cases / name; case.mkdir(); (case / "main.cpp").write_text("int main(){}")
            output = root / "table"
            calls = []

            def invoke(command, **kwargs):
                calls.append(command)
                if len(calls) == 1:
                    raise subprocess.TimeoutExpired(command, 130)
                row = dict.fromkeys(measure.COLUMNS, "")
                row.update(file="b/main.cpp", result="pruned/local", raw_bytes="20", flat_bytes="20", pruned_bytes="20")
                with Path(command[-1]).open("w", newline="") as stream:
                    writer = csv.DictWriter(stream, measure.COLUMNS, delimiter="\t")
                    writer.writeheader(); writer.writerow(row)
                return subprocess.CompletedProcess(command, 0)

            argv = ["measure", "--runner", "runner", "--worker", "worker", "--root", str(cases), "--output", str(output)]
            with patch.object(sys, "argv", argv), patch.object(measure.subprocess, "run", side_effect=invoke), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(measure.main(), 1)
            self.assertEqual(len(calls), 2)
            self.assertEqual(calls[0][:4], ["timeout", "--kill-after=5", "120", str(Path("runner").resolve())])
            with output.with_suffix(".tsv").open() as stream:
                rows = list(csv.DictReader(stream, delimiter="\t"))
            self.assertEqual([row["result"] for row in rows], ["runner-error", "pruned/local"])
            self.assertTrue(output.with_suffix(".md").exists())
            self.assertFalse(output.with_suffix(".row.tsv").exists())

    def test_markdown_escapes_rule_and_header_text(self):
        row = dict.fromkeys(measure.COLUMNS, "")
        row.update(file="x.cpp", result="flat/local", pruning_disable_reasons="<ext/a>|b")
        table = measure.markdown([row])
        self.assertIn("&lt;ext/a>&#124;b", table)
        self.assertIn("flat/local", table)
        self.assertNotIn("| diagnostic |", table)


if __name__ == "__main__":
    unittest.main()
