# SPDX-License-Identifier: GPL-3.0-or-later
from copy import deepcopy
from datetime import datetime, timedelta, timezone
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import report

from common import ROOT, envelope, result
from report import apply, prepare
from state import aggregate, body, decode, disposition, empty, transition, upstream_state


def batch(claim='availability', status='unavailable', run=1, week=0, names=('p',), sources=('s',), complete=True):
    data = envelope(claim, list(names), [result(name, source, 'upstream.test', status,
        'synthetic detail', 'deployment' if claim == 'evidence' else 'availability') for name in names for source in sources], complete=complete)
    timestamp = datetime(2026, 1, 5, 6, tzinfo=timezone.utc) + timedelta(weeks=week)
    data.update(run_id=str(run), timestamp=timestamp.isoformat(), window=list(timestamp.date().isocalendar()[:2]))
    return data


def issue(state, open=True, number=1):
    return dict(title='drift: ' + state['profile'] + ': ' + state['claim'], body=body(state), state='open' if open else 'closed', number=number)


class StateTests(unittest.TestCase):
    def test_availability_failure_repeat_and_recovery(self):
        initial = empty('p', 'availability')
        state, changed = transition(initial, batch())
        self.assertTrue(changed)
        self.assertEqual(state['sources']['s']['failures'], 1)
        self.assertIsNone(state['sources']['s']['last_success'])
        self.assertEqual(disposition(state, False, False, changed), 'open')
        repeated, changed = transition(state, batch(run=2))
        self.assertFalse(changed)
        self.assertEqual(repeated['sources']['s']['failures'], 2)
        recovered, changed = transition(repeated, batch(status='verified', run=3))
        self.assertTrue(changed)
        self.assertEqual(recovered['sources']['s']['failures'], 0)
        self.assertEqual(disposition(recovered, True, True, changed), 'closed')
        self.assertIsNotNone(recovered['last_success'])
        relapsed, changed = transition(recovered, batch(run=4))
        self.assertEqual(disposition(relapsed, False, True, changed), 'open')
        self.assertEqual(relapsed['sources']['s']['last_success'], recovered['last_success'])

    def test_evidence_and_reproduction_never_auto_close(self):
        for claim, failure, success in [('evidence', 'different facts', 'same facts'), ('reproduction', 'different', 'identical')]:
            failed, _ = transition(empty('p', claim), batch(claim, failure))
            recovered, changed = transition(failed, batch(claim, success, run=2))
            self.assertEqual(disposition(recovered, True, True, changed), 'open')
            # Human closure persists through identical repeated failures.
            repeated, changed = transition(failed, batch(claim, failure, run=2))
            self.assertFalse(changed)
            self.assertEqual(disposition(repeated, False, True, changed), 'closed')

    def test_four_consecutive_weekly_windows_with_deadline(self):
        state = empty('p', 'evidence')
        for window in range(4):
            state, changed = transition(state, batch('evidence', 'evidence unavailable', run=window + 1, week=window))
            self.assertEqual(state['sources']['s']['unavailable_windows'], window + 1)
            self.assertEqual(state['manual_review'], window == 3)
            self.assertEqual(state['status'], 'failure' if window == 3 else 'pending')
            self.assertEqual(disposition(state, False, window != 0, changed), 'open' if window == 3 else 'closed')
        self.assertEqual(state['sources']['s']['deadline'], '2026-02-09')
        repeated, changed = transition(state, batch('evidence', 'evidence unavailable', run=5, week=3))
        self.assertEqual(repeated['sources']['s']['unavailable_windows'], 4)
        self.assertFalse(changed)
        self.assertEqual(repeated['sources']['s']['deadline'], state['sources']['s']['deadline'])

    def test_unavailability_same_week_gaps_and_interrupted_windows(self):
        first, _ = transition(empty('p', 'evidence'), batch('evidence', 'evidence unavailable'))
        second, changed = transition(first, batch('evidence', 'evidence unavailable', run=2))
        self.assertEqual(second['sources']['s']['unavailable_windows'], 1)
        self.assertFalse(changed)
        gap, _ = transition(second, batch('evidence', 'evidence unavailable', run=3, week=2))
        self.assertEqual(gap['sources']['s']['unavailable_windows'], 1)
        passed, _ = transition(gap, batch('evidence', 'same facts', run=4, week=3))
        again, _ = transition(passed, batch('evidence', 'evidence unavailable', run=5, week=4))
        self.assertEqual(again['sources']['s']['unavailable_windows'], 1)

    def test_failure_classes_and_source_independence(self):
        state, _ = transition(empty('p', 'evidence'), batch('evidence', 'evidence unavailable', sources=('s', 't')))
        data = batch('evidence', 'extractor failure', run=2, week=1, sources=('s', 't'))
        data['results'][1]['status'] = 'same facts'
        state, changed = transition(state, data)
        self.assertTrue(changed)
        self.assertEqual(state['sources']['s']['failures'], 2)
        self.assertEqual(state['sources']['t']['failures'], 0)
        self.assertEqual(state['status'], 'failure')
        data['run_id'] = '3'
        data['results'][0]['status'] = 'fetch failure'
        self.assertTrue(transition(state, data)[1])

    def test_idempotence_partial_skipped_empty_and_older_run(self):
        state, _ = transition(empty('p', 'availability'), batch(run=2))
        original = deepcopy(state)
        for data in [batch(run=2), batch(run=1), batch(run=3, complete=False), batch(run=3, names=('other',))]:
            with self.subTest(data=data):
                updated, changed = transition(state, data)
                self.assertEqual(updated, state)
                self.assertFalse(changed)
        self.assertEqual(state, original)

    def test_json_roundtrip_and_comment_terminator_injection(self):
        state, _ = transition(empty('p', 'availability'), batch())
        state['sources']['s']['detail'] = '-->\n<script>evil</script>\n<!-- distill-drift-state-v1'
        rendered = body(state)
        self.assertEqual(rendered.count('<!-- distill-drift-state-v1'), 1)
        self.assertEqual(decode(rendered, 'p', 'availability'), state)
        for corrupted in ('no block', rendered + rendered, rendered.replace('"failures":1', '"failures":-1')):
            with self.assertRaises(ValueError):
                decode(corrupted, 'p', 'availability')

    def test_host_aggregation_and_selection_does_not_recover_other_profiles(self):
        one, _ = transition(empty('p', 'availability'), batch())
        two, _ = transition(empty('q', 'availability'), batch(names=('q',)))
        hosts = aggregate([one, two])
        state = upstream_state('upstream.test', hosts['upstream.test'], '1')
        self.assertEqual(state['status'], 'failure')
        one, _ = transition(one, batch(status='verified', run=2))
        partial_recovery = upstream_state('upstream.test', aggregate([one, two])['upstream.test'], '2')
        self.assertEqual(partial_recovery['status'], 'pending')
        self.assertEqual(disposition(partial_recovery, True, True, True), 'open')
        two, _ = transition(two, batch(status='verified', run=3, names=('q',)))
        recovered = upstream_state('upstream.test', aggregate([one, two])['upstream.test'], '3')
        self.assertEqual(disposition(recovered, True, True, True), 'closed')


class ReportTests(unittest.TestCase):
    def test_one_issue_per_profile_claim_and_shared_host(self):
        operations = prepare([batch(names=('p', 'q'))], [])
        self.assertEqual({o[1] for o in operations}, {'drift: p: availability', 'drift: q: availability', 'drift: upstream: upstream.test'})
        self.assertEqual(len(prepare([batch()], [])), 1)

    def test_partial_batch_anywhere_changes_no_state(self):
        for batches in [[batch(complete=False)], [batch(), batch('evidence', 'same facts', complete=False)]]:
            self.assertEqual(prepare(batches, []), [])
        self.assertEqual(prepare([batch('evidence', 'same facts', names=())], []), [])

    def test_duplicate_and_invalid_state_abort_before_api_calls(self):
        state, _ = transition(empty('p', 'availability'), batch())
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            prepare([batch(run=2)], [issue(state), issue(state)])
        invalid = issue(state)
        invalid['body'] = 'bad block'
        with self.assertRaises(ValueError):
            prepare([batch(run=2)], [invalid])

    def test_comments_only_on_transitions_and_rerun_dedupe(self):
        calls = []
        class API:
            def label(self):
                calls.append(('label',))
            def request(self, method, path, data):
                calls.append((method, path, data))
                return {'number': 1}
        state, _ = transition(empty('p', 'availability'), batch())
        apply(API(), prepare([batch(run=2)], [issue(state)]))
        self.assertFalse(any(len(c) > 1 and c[1].endswith('/comments') for c in calls))
        calls.clear()
        apply(API(), prepare([batch(status='verified', run=2)], [issue(state)]))
        self.assertEqual(sum(len(c) > 1 and c[1].endswith('/comments') for c in calls), 1)
        self.assertEqual(calls[1][2]['state'], 'closed')
        self.assertEqual(prepare([batch()], [issue(state)]), [])

    def test_pending_evidence_tracker_is_closed_and_no_source_means_no_issue(self):
        operations = prepare([batch('evidence', 'evidence unavailable')], [])
        self.assertEqual(disposition(operations[0][2], False, False, operations[0][3]), 'closed')
        data = batch('evidence', 'same facts')
        data['results'] = []
        self.assertEqual(prepare([data], []), [])

    def test_evidence_aggregate_never_auto_closes(self):
        p, _ = transition(empty('p', 'evidence'), batch('evidence', 'same facts'))
        q, _ = transition(empty('q', 'evidence'), batch('evidence', 'same facts', names=('q',)))
        recovered = upstream_state('upstream.test', aggregate([p, q])['upstream.test'], '2')
        self.assertEqual(disposition(recovered, True, True, True), 'open')


class ReportDiagnosticTests(unittest.TestCase):
    def test_output_retains_plan_if_apply_fails_and_records_completion(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            output = Path(directory) / 'report.json'
            args = ['--claims', 'reproduction', '--profiles', 'all',
                    '--artifacts', directory, '--output', str(output)]
            operations = [(None, 'drift: p: reproduction', {'status': 'failure'}, True)]
            with patch.object(report, 'claims', return_value=[]), \
                    patch.object(report, 'GitHub'), \
                    patch.object(report, 'prepare', return_value=operations), \
                    patch.dict(os.environ, GITHUB_RUN_ID='123'), \
                    patch.object(report, 'apply', side_effect=OSError('synthetic API failure')), \
                    patch('sys.stdout', new_callable=io.StringIO):
                with self.assertRaises(OSError):
                    report.main(args)
                data = json.loads(output.read_text())
                self.assertEqual(data['status'], 'prepared')
                self.assertEqual(data['operations'][0]['title'], 'drift: p: reproduction')
                self.assertEqual(data['run_id'], '123')
            with patch.object(report, 'claims', return_value=[]), \
                    patch.object(report, 'GitHub'), \
                    patch.object(report, 'prepare', return_value=operations), \
                    patch.dict(os.environ, GITHUB_RUN_ID='123'), \
                    patch.object(report, 'apply'), \
                    patch('sys.stdout', new_callable=io.StringIO):
                report.main(args)
                self.assertEqual(json.loads(output.read_text())['status'], 'applied')
