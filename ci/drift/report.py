# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate result artifacts, then maintain drift issues with GitHub's REST API."""
import json
import os
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import Request, urlopen

import availability
import evidence
import reproduction
import library
from common import CLAIMS, arguments, profiles, validate, write
from plan import claims
from state import aggregate, body, decode, disposition, empty, transition, upstream_state


class GitHub:
    def __init__(self):
        self.base = 'https://api.github.com/repos/' + os.environ['GITHUB_REPOSITORY']
        self.headers = {'Authorization': 'Bearer ' + os.environ['GITHUB_TOKEN'], 'Accept': 'application/vnd.github+json',
                        'X-GitHub-Api-Version': '2022-11-28', 'User-Agent': 'distill-drift/1'}

    def request(self, method, path, data=None):
        payload = json.dumps(data).encode() if data is not None else None
        headers = dict(self.headers, **({'Content-Type': 'application/json'} if payload is not None else {}))
        with urlopen(Request(self.base + path, data=payload, headers=headers, method=method), timeout=60) as response:
            return json.load(response) if response.status != 204 else None

    def issues(self):
        issues, page = [], 1
        while True:
            rows = self.request('GET', f'/issues?state=all&per_page=100&page={page}')
            issues.extend(row for row in rows if 'pull_request' not in row)
            if len(rows) < 100:
                return issues
            page += 1

    def label(self):
        try:
            self.request('GET', '/labels/drift')
        except HTTPError as error:
            if error.code != 404:
                raise
            self.request('POST', '/labels', {'name': 'drift', 'color': 'b60205', 'description': 'Profile maintenance'})


def prepare(batches, issues):
    """Pure planning: decode *all* affected state before the first API mutation."""
    indexed = {}
    persisted = {}
    for issue in issues:
        title = issue['title']
        if not title.startswith('drift: '):
            continue
        if title in indexed:
            raise ValueError('duplicate maintenance issue: ' + title)
        indexed[title] = issue
        parts = title.split(': ')
        if len(parts) == 3 and parts[1] != 'upstream' and parts[2] in CLAIMS:
            persisted[(parts[1], parts[2])] = decode(issue['body'] or '', parts[1], parts[2])
    operations = []
    for batch in batches:
        if not batch['complete']:
            return []
        for name in batch['profiles']:
            key = name, batch['claim']
            old = persisted.get(key, empty(*key))
            new, changed = transition(old, batch)
            if new == old:
                continue
            persisted[key] = new
            title = 'drift: ' + name + ': ' + batch['claim']
            operations.append((title, new, changed))
    # Aggregation uses all source states, including profiles not selected now.
    if not operations:
        return []
    touched_hosts = {row['host'] for batch in batches for row in batch['results']}
    current = aggregate(persisted.values())
    for host, sources in current.items():
        if host not in touched_hosts:
            continue
        title = 'drift: upstream: ' + host
        new = upstream_state(host, sources, max((b['run_id'] for b in batches), key=int))
        issue = indexed.get(title)
        if not issue and new['status'] != 'failure':
            continue
        old = decode(issue['body'] or '', new['profile'], new['claim']) if issue else empty(new['profile'], new['claim'])
        if int(new['last_run_id']) <= int(old['last_run_id']):
            continue
        changed = old['status'] != new['status'] or {k: v['status'] for k, v in old['sources'].items()} != {k: v['status'] for k, v in sources.items()}
        operations.append((title, new, changed))
    return [(indexed.get(title), title, state, changed) for title, state, changed in operations]


def apply(api, operations):
    if not operations:
        return
    api.label()
    for issue, title, state, changed in operations:
        status = disposition(state, issue is not None and issue['state'] == 'open', issue is not None, changed)
        payload = {'body': body(state), 'state': status}
        if issue is None:
            created = api.request('POST', '/issues', {'title': title, 'body': payload['body'], 'labels': ['drift']})
            if status == 'closed':
                api.request('PATCH', '/issues/' + str(created['number']), {'state': 'closed'})
        else:
            number = str(issue['number'])
            payload['labels'] = sorted({label['name'] for label in issue.get('labels', [])} | {'drift'})
            api.request('PATCH', '/issues/' + number, payload)
            if changed:
                api.request('POST', '/issues/' + number + '/comments', {'body': 'Maintenance state transition: **' + state['status'] + '**. Run ' + state['last_run_id'] + '. See the issue body for per-source status and review deadlines.'})


def build_parser():
    parser = arguments(__doc__)
    parser.add_argument('--claims', required=True)
    parser.add_argument('--artifacts', type=Path, required=True)
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    selected = profiles(args.profiles)
    batches = []
    modules = {'availability': availability, 'evidence': evidence, 'reproduction': reproduction, 'library': library}
    for claim in claims(args.claims):
        path = args.artifacts / ('drift-' + claim) / (claim + '.json')
        # Missing/invalid/partial artifacts cause no state changes to any claim.
        if path.stat().st_size > 8 * 1024 * 1024:
            raise ValueError('oversize result artifact')
        data = json.loads(path.read_text())
        batches.append(validate(data, claim, selected, modules[claim].expected(selected), os.environ['GITHUB_RUN_ID']))
    api = GitHub()
    operations = prepare(batches, api.issues())
    diagnostic = dict(run_id=os.environ['GITHUB_RUN_ID'], status='prepared', operations=[
        dict(issue=issue['number'] if issue else None, title=title, state=state, changed=changed)
        for issue, title, state, changed in operations])
    # Persist the validated plan before writes so partial API failures are debuggable.
    write(args.output, diagnostic)
    print(f'report: {len(operations)} issue updates prepared', flush=True)
    apply(api, operations)
    diagnostic['status'] = 'applied'
    write(args.output, diagnostic)
    print('report: issue updates applied')


if __name__ == '__main__':
    main()
