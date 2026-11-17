# SPDX-License-Identifier: GPL-3.0-or-later
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from common import ROOT
from plan import claims


class PlanningTests(unittest.TestCase):
    def test_claim_selection_rejects_shell_and_unknown_inputs(self):
        self.assertEqual(claims('availability,reproduction'), ['availability', 'reproduction'])
        for bad in ('', 'availability,availability', 'availability,', '$(echo bad)', 'other'):
            with self.assertRaises(ValueError):
                claims(bad)

    def test_schedule_and_dispatch_plan_outputs(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            output = Path(directory) / 'output'
            for event, cron, selected, expected in [
                ('schedule', '17 6 * * 1', '', 'evidence,availability'),
                ('schedule', '23 6 1 * *', '', 'reproduction'),
                ('workflow_dispatch', '', 'availability', 'availability')]:
                env = dict(os.environ, EVENT_NAME=event, SCHEDULE=cron, CLAIMS=selected,
                           PROFILES='cses-gcc13-cpp23', GITHUB_OUTPUT=str(output))
                output.write_text('')
                subprocess.run([sys.executable, str(ROOT / 'ci/drift/plan.py')], env=env, check=True, capture_output=True)
                rows = dict(line.split('=', 1) for line in output.read_text().splitlines())
                self.assertEqual(rows['claims'], expected)
                self.assertEqual(rows['profiles'], 'cses-gcc13-cpp23')
                self.assertEqual(len(rows), 5)

    def test_existing_profile_plan_writes_real_output_lines(self):
        # Execute only the local Python planner embedded in the workflow.
        # No workflow, downloads or builds are run.
        text = (ROOT / '.github/workflows/profiles.yml').read_text()
        code = text.split("python3 - <<'PY'\n", 1)[1].split('\n          PY', 1)[0]
        code = '\n'.join(line[10:] for line in code.splitlines())
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            output = Path(directory) / 'output'
            env = dict(os.environ, PROFILES='cses-gcc13-cpp23', GITHUB_OUTPUT=str(output))
            subprocess.run([sys.executable, '-c', code], cwd=ROOT, env=env, check=True, capture_output=True)
            rows = dict(line.split('=', 1) for line in output.read_text().splitlines())
            self.assertEqual(set(rows), {'runner_matrix', 'container_matrix', 'has_runner', 'has_container'})
            self.assertTrue(json.loads(rows['has_runner']))
            self.assertFalse(json.loads(rows['has_container']))
