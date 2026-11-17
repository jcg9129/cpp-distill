# SPDX-License-Identifier: GPL-3.0-or-later
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from common import ROOT, profiles
import availability
import evidence
import reproduction
from plan import claims


class PlanningTests(unittest.TestCase):
    def test_every_shipped_profile_has_all_three_claims(self):
        shipped = {path.stem for path in (ROOT / 'data/judges').glob('*.toml')}
        selected = profiles()
        self.assertEqual(set(selected), shipped)
        for claim in (availability, reproduction, evidence):
            with self.subTest(claim=claim.__name__):
                self.assertEqual({name for name, source in claim.expected(selected)}, shipped)

    def test_claim_selection_rejects_shell_and_unknown_inputs(self):
        self.assertEqual(claims('availability,reproduction'), ['availability', 'reproduction'])
        for bad in ('', 'availability,availability', 'availability,', '$(echo bad)', 'other'):
            with self.assertRaises(ValueError):
                claims(bad)

    def test_schedule_and_dispatch_plan_outputs(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            output = Path(directory) / 'output'
            for event, cron, selected, expected, selection in [
                ('schedule', '17 6 * * 1', '', 'evidence,availability', ''),
                ('schedule', '23 6 1 * *', '', 'reproduction', 'all'),
                ('workflow_dispatch', '', 'availability', 'availability', 'timus-gcc13-cpp23')]:
                env = dict(os.environ, EVENT_NAME=event, SCHEDULE=cron, CLAIMS=selected,
                           PROFILES=selection, GITHUB_OUTPUT=str(output))
                output.write_text('')
                subprocess.run([sys.executable, str(ROOT / 'ci/drift/plan.py')], env=env, check=True, capture_output=True)
                rows = dict(line.split('=', 1) for line in output.read_text().splitlines())
                self.assertEqual(rows['claims'], expected)
                self.assertEqual(rows['profiles'].split(','), profiles(selection or 'all'))
                self.assertEqual(len(rows), 5)

    def test_existing_profile_plan_writes_real_output_lines(self):
        # Execute only the local Python planner embedded in the workflow.
        # No workflow, downloads or builds are run.
        text = (ROOT / '.github/workflows/profiles.yml').read_text()
        code = text.split("python3 - <<'PY'\n", 1)[1].split('\n          PY', 1)[0]
        code = '\n'.join(line[10:] for line in code.splitlines())
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            output = Path(directory) / 'output'
            for selection in ('cses-gcc13-cpp23', 'timus-gcc13-cpp23', 'all'):
                with self.subTest(selection=selection):
                    output.write_text('')
                    env = dict(os.environ, PROFILES=selection, GITHUB_OUTPUT=str(output))
                    subprocess.run([sys.executable, '-c', code], cwd=ROOT, env=env, check=True, capture_output=True)
                    rows = dict(line.split('=', 1) for line in output.read_text().splitlines())
                    self.assertEqual(set(rows), {'runner_matrix', 'container_matrix', 'has_runner', 'has_container'})
                    matrices = []
                    for kind in ('runner', 'container'):
                        matrix = json.loads(rows[kind + '_matrix'])['include']
                        self.assertEqual(json.loads(rows['has_' + kind]), bool(matrix))
                        matrices.extend(matrix)
                    self.assertEqual({row['profile'] for row in matrices}, set(profiles(selection)))
                    if selection != 'cses-gcc13-cpp23':
                        timus = next(row for row in matrices if row['profile'] == 'timus-gcc13-cpp23')
                        self.assertEqual(timus['acquisition'], 'winlibs')
                        self.assertEqual(timus['runner'], 'windows-2025')
