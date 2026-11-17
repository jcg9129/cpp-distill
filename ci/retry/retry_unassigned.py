# SPDX-License-Identifier: GPL-3.0-or-later
"""Conservatively retry unassigned jobs using only the trusted event and REST API."""

import json
import os
from pathlib import Path
import re
import sys
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


RETRY_WORKFLOW = 'Retry unassigned jobs'
# Keep in sync with retry-unassigned.yml; Judge macro snapshots has no macOS jobs.
WORKFLOWS = {
    'Core unit tests', 'Compatibility matrix', 'Distribution spike', 'Release',
    'A1 reachable-header extension inventories', 'LLVM SDK',
    'Official LLVM archive audit',
}


def positive_integer(value):
    return type(value) is int and value > 0


def unassigned(job):
    """Missing fields are not evidence: require an explicit empty steps array.

    Both runner fields must be present and unset, including GitHub's zero ID.
    started_at is not used: it does not establish that a runner executed a step.
    """
    return (job.get('conclusion') == 'cancelled'
            and job.get('steps') == []
            and 'runner_name' in job and job['runner_name'] in (None, '')
            and 'runner_id' in job
            and (job['runner_id'] is None
                 or (type(job['runner_id']) is int and job['runner_id'] == 0)))


def skip_run(run, repository):
    """Return a skip reason, or None if the run is eligible for inspection."""
    if (run.get('name') == RETRY_WORKFLOW
            or run.get('path', '').split('@')[0] == '.github/workflows/retry-unassigned.yml'):
        return 'retry workflow itself'
    if run.get('name') not in WORKFLOWS:
        return 'workflow is not eligible'
    head = run.get('head_repository') or {}
    if (head.get('full_name', '').casefold() != repository.casefold()
            or head.get('fork') is not False):
        return 'fork or unknown head repository'
    attempt = run.get('run_attempt')
    if not positive_integer(attempt):
        return 'missing or invalid run_attempt'
    if attempt >= 3:
        return 'run_attempt limit reached (3)'
    if run.get('status') != 'completed':
        return 'run is not completed'
    if run.get('conclusion') not in ('cancelled', 'failure'):
        return 'run conclusion is not cancelled or failure'
    if not positive_integer(run.get('id')):
        return 'missing or invalid run ID'
    return None


def decide(run, jobs, repository):
    """Return (should_retry, reason), without IO.

    Every non-success must be unassigned, including skipped/neutral jobs. For an
    overall cancelled run, require a successful job in this same attempt. The
    jobs API has no cancellation cause; elapsed time alone is not timeout proof.
    """
    reason = skip_run(run, repository)
    if reason:
        return False, reason
    if not jobs:
        return False, 'attempt has no jobs'
    if any(job.get('status') != 'completed' for job in jobs):
        return False, 'attempt has incomplete jobs'
    candidates = [job for job in jobs if unassigned(job)]
    if not candidates:
        return False, 'no unassigned jobs'
    if any(job.get('conclusion') != 'success' and not unassigned(job) for job in jobs):
        return False, 'a non-successful job is not unassigned; human review required'
    if (run['conclusion'] == 'cancelled'
            and not any(job.get('conclusion') == 'success' for job in jobs)):
        return False, 'cancelled attempt has no successful job; possible user cancellation'
    return True, f'{len(candidates)} unassigned job(s); all other jobs succeeded'


class GitHub:
    def __init__(self, repository, token, api_url='https://api.github.com'):
        if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository):
            raise ValueError('invalid repository')
        self.base = f'{api_url.rstrip("/")}/repos/{repository}/actions/runs'
        self.token = token

    def request(self, method, path):
        request = Request(self.base + path, method=method, headers={
            'Accept': 'application/vnd.github+json',
            'Authorization': f'Bearer {self.token}',
            'User-Agent': 'cpp-distill-retry-unassigned',
            'X-GitHub-Api-Version': '2022-11-28',
        })
        with urlopen(request, timeout=30) as response:
            if method == 'POST':
                if response.status != 201:
                    raise ValueError('rerun API did not return 201')
                return None
            return json.load(response)

    def jobs(self, run_id, attempt):
        # Attempt-specific endpoint avoids mixing successful jobs from old attempts.
        jobs = []
        total = None
        page = 1
        while True:
            payload = self.request('GET',
                f'/{run_id}/attempts/{attempt}/jobs?per_page=100&page={page}')
            count = payload['total_count']
            batch = payload['jobs']
            if (type(count) is not int or count < 0 or not isinstance(batch, list)
                    or (total is not None and count != total)):
                raise ValueError('invalid or changing jobs pagination')
            total = count
            jobs.extend(batch)
            if (len(jobs) > total or (not batch and len(jobs) < total)
                    or any(not positive_integer(job.get('id'))
                           or job.get('run_id') != run_id for job in jobs)
                    or len({job['id'] for job in jobs}) != len(jobs)):
                raise ValueError('incomplete or inconsistent attempt jobs')
            if len(jobs) == total:
                return jobs
            page += 1


def process(event, repository, api):
    """Inspect the event attempt and recheck freshness before the single POST."""
    run = event['workflow_run']
    reason = skip_run(run, repository)
    if event.get('action') != 'completed':
        reason = 'event is not completed'
    if reason:
        return 'skip', reason
    run_id, attempt = run['id'], run['run_attempt']
    jobs = api.jobs(run_id, attempt)
    retry, reason = decide(run, jobs, repository)
    if not retry:
        return 'skip', reason
    # Serialized workflow runs plus this check avoid duplicate deliveries and
    # delayed events rerunning a newer attempt (including a human's rerun).
    current = api.request('GET', f'/{run_id}')
    if current.get('id') != run_id or current.get('run_attempt') != attempt:
        return 'skip', 'run attempt changed since event'
    retry, current_reason = decide(current, jobs, repository)
    if not retry:
        return 'skip', current_reason
    api.request('POST', f'/{run_id}/rerun-failed-jobs')
    return 'rerun', reason


def main():
    summary = {}
    try:
        event = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text(encoding='utf-8'))
        run = event['workflow_run']
        summary.update(run_id=run.get('id'), run_attempt=run.get('run_attempt'))
        repository = os.environ['GITHUB_REPOSITORY']
        api = GitHub(repository, os.environ['GITHUB_TOKEN'],
                     os.environ.get('GITHUB_API_URL', 'https://api.github.com'))
        decision, reason = process(event, repository, api)
        summary.update(decision=decision, reason=reason)
    except HTTPError as error:
        summary.update(decision='error', reason=f'GitHub API returned HTTP {error.code}')
    except (AttributeError, KeyError, TypeError, ValueError, OSError, URLError):
        summary.update(decision='error', reason='could not read event or complete API inspection')
    # JSON keeps event strings on one line and prevents Actions log commands.
    print(json.dumps(summary, sort_keys=True))
    return 1 if summary['decision'] == 'error' else 0


if __name__ == '__main__':
    sys.exit(main())
