# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthetic REST/event fixtures; every HTTP request is intercepted."""

from contextlib import redirect_stdout
from copy import deepcopy
import io
import json
from pathlib import Path
import unittest
from unittest.mock import patch
from urllib.error import HTTPError, URLError

import retry_unassigned as retry


FIXTURES = Path(__file__).parent / 'fixtures'
REPOSITORY = 'jcg9129/cpp-distill'


def fixture(name):
    return json.loads((FIXTURES / name).read_text(encoding='utf-8'))


def response(payload, status=200):
    stream = io.BytesIO(json.dumps(payload).encode())
    stream.status = status
    return stream


class RetryTests(unittest.TestCase):
    def setUp(self):
        self.event = fixture('completed-event.json')
        self.run = self.event['workflow_run']
        self.pages = [fixture(f'jobs-page-{page}.json') for page in (1, 2)]
        self.jobs = self.pages[0]['jobs'] + self.pages[1]['jobs']
        self.api = retry.GitHub(REPOSITORY, 'test-token')
        # Fail instead of using the network, including in tests of the CLI.
        self.opener = self.enterContext(patch.object(
            retry, 'urlopen', side_effect=AssertionError('unexpected HTTP request')))

    def serve(self, *payloads):
        self.opener.side_effect = [response(payload, status) for payload, status in payloads]

    def routes(self):
        return [(call.args[0].method, call.args[0].full_url)
                for call in self.opener.call_args_list]

    def test_explicit_unassigned_runner_variants(self):
        for name in (None, ''):
            for runner_id in (None, 0):
                with self.subTest(name=name, runner_id=runner_id):
                    job = dict(self.jobs[1], runner_name=name, runner_id=runner_id)
                    self.assertTrue(retry.unassigned(job))

    def test_incomplete_or_assigned_jobs_are_not_unassigned(self):
        for key in ('conclusion', 'steps', 'runner_name', 'runner_id'):
            with self.subTest(missing=key):
                job = dict(self.jobs[1])
                del job[key]
                self.assertFalse(retry.unassigned(job))
        for changes in (
            {'conclusion': 'failure'}, {'conclusion': 'timed_out'},
            {'steps': None}, {'steps': {}}, {'steps': ''},
            {'steps': [{'name': 'Set up job', 'conclusion': 'cancelled'}]},
            {'runner_id': 42}, {'runner_id': '0'}, {'runner_id': False},
            {'runner_id': 0.0}, {'runner_name': 'assigned'},
            {'runner_name': ' '}, {'runner_name': 0},
        ):
            with self.subTest(changes=changes):
                self.assertFalse(retry.unassigned(dict(self.jobs[1], **changes)))

    def test_retry_attempts_one_and_two(self):
        for attempt in (1, 2):
            for conclusion in ('cancelled', 'failure'):
                with self.subTest(attempt=attempt, conclusion=conclusion):
                    run = dict(self.run, run_attempt=attempt, conclusion=conclusion)
                    self.assertEqual(retry.decide(run, self.jobs, REPOSITORY),
                                     (True, '2 unassigned job(s); all other jobs succeeded'))

    def test_all_eligible_workflows(self):
        for name in retry.WORKFLOWS:
            with self.subTest(name=name):
                self.assertTrue(retry.decide(dict(self.run, name=name), self.jobs, REPOSITORY)[0])

    def test_attempt_cap_and_invalid_attempts(self):
        for attempt in (3, 4, 0, -1, None, '1', True):
            with self.subTest(attempt=attempt):
                self.assertFalse(retry.decide(dict(self.run, run_attempt=attempt),
                                              self.jobs, REPOSITORY)[0])

    def test_real_failure_or_started_cancellation_blocks_entire_attempt(self):
        for changes in (
            {'conclusion': 'failure'}, {'conclusion': 'timed_out'},
            {'conclusion': 'action_required'}, {'conclusion': 'skipped'},
            {'conclusion': 'neutral'}, {'conclusion': None},
            {'steps': [{'name': 'Set up job'}]},
            {'runner_name': 'assigned'}, {'runner_id': 123},
        ):
            with self.subTest(changes=changes):
                jobs = [self.jobs[0], self.jobs[1], dict(self.jobs[2], **changes)]
                decision, reason = retry.decide(self.run, jobs, REPOSITORY)
                self.assertFalse(decision)
                self.assertIn('human review', reason)

    def test_all_unassigned_cancellation_is_ambiguous_even_after_fifteen_minutes(self):
        for jobs in (self.jobs[1:], [self.jobs[1]], [self.jobs[2]]):
            decision, reason = retry.decide(self.run, jobs, REPOSITORY)
            self.assertFalse(decision)
            self.assertIn('possible user cancellation', reason)

    def test_failure_conclusion_with_only_unassigned_jobs(self):
        self.assertTrue(retry.decide(dict(self.run, conclusion='failure'),
                                    self.jobs[1:], REPOSITORY)[0])

    def test_empty_successful_or_incomplete_attempt(self):
        for jobs in ([], [self.jobs[0]],
                     [self.jobs[0], dict(self.jobs[1], status='in_progress')]):
            with self.subTest(jobs=jobs):
                self.assertFalse(retry.decide(self.run, jobs, REPOSITORY)[0])

    def test_run_guardrails_skip_without_http(self):
        for changes in (
            {'head_repository': {'full_name': 'contributor/cpp-distill', 'fork': True}},
            {'head_repository': {'full_name': 'contributor/cpp-distill', 'fork': False}},
            {'head_repository': {'full_name': REPOSITORY, 'fork': True}},
            {'head_repository': {'full_name': REPOSITORY}},
            {'head_repository': None}, {'head_repository': {}},
            {'name': retry.RETRY_WORKFLOW},
            {'path': '.github/workflows/retry-unassigned.yml@main'},
            {'name': 'Judge macro snapshots'}, {'name': 'unknown'},
            {'run_attempt': 3}, {'status': 'in_progress'},
            {'conclusion': 'success'}, {'conclusion': 'timed_out'}, {'id': None},
        ):
            with self.subTest(changes=changes):
                event = dict(self.event, workflow_run=dict(self.run, **changes))
                self.assertEqual(retry.process(event, REPOSITORY, self.api)[0], 'skip')
        self.opener.assert_not_called()

    def test_non_completed_event_skips_without_http(self):
        self.event['action'] = 'requested'
        self.assertEqual(retry.process(self.event, REPOSITORY, self.api)[0], 'skip')
        self.opener.assert_not_called()

    def test_pagination_current_attempt_and_exact_rerun_request(self):
        self.serve((self.pages[0], 200), (self.pages[1], 200), (self.run, 200), (None, 201))
        self.assertEqual(retry.process(self.event, REPOSITORY, self.api)[0], 'rerun')
        base = 'https://api.github.com/repos/jcg9129/cpp-distill/actions/runs/123456789'
        self.assertEqual(self.routes(), [
            ('GET', base + '/attempts/1/jobs?per_page=100&page=1'),
            ('GET', base + '/attempts/1/jobs?per_page=100&page=2'),
            ('GET', base), ('POST', base + '/rerun-failed-jobs'),
        ])
        for call in self.opener.call_args_list:
            request = call.args[0]
            self.assertEqual(request.get_header('Authorization'), 'Bearer test-token')
            self.assertEqual(request.get_header('X-github-api-version'), '2022-11-28')
            self.assertEqual(call.kwargs['timeout'], 30)
        self.assertIsNone(self.opener.call_args.args[0].data)

    def test_second_attempt_uses_second_attempt_endpoint(self):
        self.run['run_attempt'] = 2
        self.serve(({'total_count': 3, 'jobs': self.jobs}, 200), (self.run, 200), (None, 201))
        self.assertEqual(retry.process(self.event, REPOSITORY, self.api)[0], 'rerun')
        self.assertIn('/attempts/2/jobs?', self.routes()[0][1])

    def test_failure_on_later_page_prevents_post(self):
        self.pages[1]['jobs'][0]['conclusion'] = 'failure'
        self.serve((self.pages[0], 200), (self.pages[1], 200))
        decision, reason = retry.process(self.event, REPOSITORY, self.api)
        self.assertEqual(decision, 'skip')
        self.assertIn('human review', reason)
        self.assertEqual(len(self.routes()), 2)

    def test_changed_or_ineligible_current_run_prevents_post(self):
        for changes in (
            {'run_attempt': 2}, {'id': 42}, {'status': 'queued'},
            {'conclusion': 'success'}, {'name': retry.RETRY_WORKFLOW},
            {'head_repository': None},
        ):
            with self.subTest(changes=changes):
                self.opener.reset_mock()
                self.serve(({'total_count': 3, 'jobs': self.jobs}, 200),
                           (dict(self.run, **changes), 200))
                self.assertEqual(retry.process(self.event, REPOSITORY, self.api)[0], 'skip')
                self.assertTrue(all(method == 'GET' for method, _ in self.routes()))

    def test_invalid_or_incomplete_pagination_fails_closed(self):
        first = deepcopy(self.pages[0])
        invalid = [
            {'total_count': 3, 'jobs': []},
            {'total_count': 4, 'jobs': self.pages[1]['jobs']},
            {'total_count': 3, 'jobs': [self.jobs[1]]},  # duplicate ID
            {'total_count': 3, 'jobs': [dict(self.jobs[2], run_id=42)]},
            {'total_count': 3, 'jobs': [dict(self.jobs[2], id=None)]},
            {'total_count': 3, 'jobs': [self.jobs[2], dict(self.jobs[2], id=104)]},
            {'total_count': '3', 'jobs': []},
            {'total_count': 3, 'jobs': None},
        ]
        for second in invalid:
            with self.subTest(second=second):
                self.serve((first, 200), (second, 200))
                with self.assertRaises(ValueError):
                    retry.process(self.event, REPOSITORY, self.api)
        self.assertTrue(all(method == 'GET' for method, _ in self.routes()))

    def test_zero_jobs_is_a_skip(self):
        self.serve(({'total_count': 0, 'jobs': []}, 200))
        self.assertEqual(retry.process(self.event, REPOSITORY, self.api),
                         ('skip', 'attempt has no jobs'))

    def run_main(self):
        with patch.dict('os.environ', {
            'GITHUB_EVENT_PATH': str(FIXTURES / 'completed-event.json'),
            'GITHUB_REPOSITORY': REPOSITORY, 'GITHUB_TOKEN': 'test-token',
        }, clear=True), redirect_stdout(io.StringIO()) as output:
            status = retry.main()
        lines = output.getvalue().splitlines()
        self.assertEqual(len(lines), 1)
        self.assertNotIn('test-token', lines[0])
        return status, json.loads(lines[0])

    def test_cli_logs_one_line_after_rerun(self):
        self.serve(({'total_count': 3, 'jobs': self.jobs}, 200), (self.run, 200), (None, 201))
        status, summary = self.run_main()
        self.assertEqual(status, 0)
        self.assertEqual(summary['decision'], 'rerun')
        self.assertEqual(summary['run_id'], self.run['id'])
        self.assertEqual(summary['run_attempt'], 1)

    def test_cli_logs_skip_reason(self):
        self.serve(({'total_count': 1, 'jobs': [self.jobs[1]]}, 200))
        status, summary = self.run_main()
        self.assertEqual(status, 0)
        self.assertEqual(summary['decision'], 'skip')
        self.assertIn('possible user cancellation', summary['reason'])

    def test_malformed_event_or_api_shape_logs_one_error_line(self):
        for payload in ({}, {'workflow_run': None}, {'workflow_run': []}):
            with self.subTest(payload=payload), patch.object(
                    retry.Path, 'read_text', return_value=json.dumps(payload)):
                status, summary = self.run_main()
                self.assertEqual(status, 1)
                self.assertEqual(summary['decision'], 'error')
        self.opener.assert_not_called()
        self.serve(({'total_count': 1, 'jobs': [None]}, 200))
        status, summary = self.run_main()
        self.assertEqual(status, 1)
        self.assertEqual(summary['decision'], 'error')

    def test_http_network_and_json_errors_log_one_line_without_post(self):
        for error in (HTTPError('https://api.github.com', 403, 'Forbidden', {}, None),
                      URLError('offline'), ValueError('bad JSON'), KeyError('jobs')):
            with self.subTest(error=error):
                self.opener.side_effect = error
                status, summary = self.run_main()
                self.assertEqual(status, 1)
                self.assertEqual(summary['decision'], 'error')
        self.assertTrue(all(method == 'GET' for method, _ in self.routes()))

    def test_post_error_is_not_retried(self):
        self.opener.side_effect = [
            response({'total_count': 3, 'jobs': self.jobs}), response(self.run),
            HTTPError('https://api.github.com', 403, 'Forbidden', {}, None),
        ]
        status, summary = self.run_main()
        self.assertEqual(status, 1)
        self.assertEqual(summary['reason'], 'GitHub API returned HTTP 403')
        self.assertEqual([method for method, _ in self.routes()].count('POST'), 1)


if __name__ == '__main__':
    unittest.main()
