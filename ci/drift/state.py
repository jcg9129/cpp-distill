# SPDX-License-Identifier: GPL-3.0-or-later
"""Pure issue state transitions. No filesystem, network, process clock or API calls."""
from copy import deepcopy
from datetime import date, datetime, timedelta
import json
import re

from common import PASSES

MARKER = 'distill-drift-state-v1'


def empty(profile, claim):
    return dict(schema=1, profile=profile, claim=claim, sources={}, last_run_id='0',
                last_success=None, status='pass', manual_review=False)


def decode(body, profile, claim):
    matches = re.findall(r'<!-- ' + MARKER + r'\n(.*?)\n-->', body, re.S)
    if len(matches) != 1:
        raise ValueError('issue needs exactly one drift state block')
    state = json.loads(matches[0])
    if (not isinstance(state, dict) or set(state) != set(empty(profile, claim))
            or state['schema'] != 1 or state['profile'] != profile or state['claim'] != claim
            or state['status'] not in ('pass', 'pending', 'failure')
            or not isinstance(state['sources'], dict) or not str(state['last_run_id']).isdigit()
            or type(state['manual_review']) is not bool):
        raise ValueError('invalid issue state envelope')
    if state['last_success'] is not None:
        datetime.fromisoformat(state['last_success'])
    required = {'status', 'failures', 'unavailable_windows', 'last_window', 'last_success', 'last_run_id', 'host', 'kind', 'detail', 'deadline'}
    for key, source in state['sources'].items():
        if not isinstance(key, str) or not isinstance(source, dict) or set(source) != required:
            raise ValueError('invalid issue source state')
        if any(type(source[k]) is not int or source[k] < 0 for k in ('failures', 'unavailable_windows')):
            raise ValueError('invalid issue failure counter')
        if not isinstance(source['last_window'], list) or len(source['last_window']) != 2:
            raise ValueError('invalid issue weekly window')
        date.fromisocalendar(*source['last_window'], 1)
        if source['last_success'] is not None:
            datetime.fromisoformat(source['last_success'])
        if source['deadline'] is not None:
            date.fromisoformat(source['deadline'])
        if not str(source['last_run_id']).isdigit() or any(not isinstance(source[k], str) for k in ('host', 'kind', 'detail', 'status')):
            raise ValueError('invalid issue source field')
    return state


def transition(previous, batch):
    """Return new state + whether to comment. Partial/cancelled/skipped = no-op.

    Complete result envelopes have already passed common.validate. Only the
    selected profile's rows enter this transition. A repeated/older run ID is
    ignored, including a GitHub rerun with a different run_attempt.
    """
    if not batch['complete'] or int(batch['run_id']) <= int(previous['last_run_id']):
        return deepcopy(previous), False
    rows = [r for r in batch['results'] if r['profile'] == previous['profile']]
    if not rows:
        return deepcopy(previous), False
    state = deepcopy(previous)
    # Complete source coverage makes source removals explicit, rather than
    # leaving dead watches permanently failing after a reviewed watch change.
    state['sources'] = {}
    for row in rows:
        old = previous['sources'].get(row['source'])
        passed = row['status'] in PASSES
        window = list(batch['window'])
        unavailable = 0
        if row['status'] == 'evidence unavailable':
            unavailable = 1
            if old and old['status'] == 'evidence unavailable':
                delta = (date.fromisocalendar(*window, 1) - date.fromisocalendar(*old['last_window'], 1)).days
                if delta == 0:
                    unavailable = old['unavailable_windows']
                elif delta == 7:
                    unavailable = old['unavailable_windows'] + 1
        deadline = None
        if unavailable >= 4:
            deadline = (old.get('deadline') if old else None) or (datetime.fromisoformat(batch['timestamp']).date() + timedelta(days=14)).isoformat()
        state['sources'][row['source']] = dict(
            status=row['status'], failures=0 if passed else (old['failures'] if old else 0) + 1,
            unavailable_windows=unavailable, last_window=window,
            last_success=batch['timestamp'] if passed else old['last_success'] if old else None,
            last_run_id=batch['run_id'], host=row['host'], kind=row['kind'], detail=row['detail'], deadline=deadline)
    state['last_run_id'] = batch['run_id']
    values = state['sources'].values()
    state['manual_review'] = any(s['unavailable_windows'] >= 4 for s in values)
    failing = [s for s in values if s['status'] not in PASSES]
    # An unavailable evidence source gets four *weekly windows*, not four runs.
    # It remains visibly pending in its tracking issue before the deadline.
    state['status'] = ('pass' if not failing else 'pending' if state['claim'] == 'evidence'
                       and all(s['status'] == 'evidence unavailable' and s['unavailable_windows'] < 4 for s in failing) else 'failure')
    if not failing:
        state['last_success'] = batch['timestamp']
    def signature(s):
        return (s['status'], s['manual_review'], {k: (v['status'], v['unavailable_windows'] >= 4) for k, v in s['sources'].items()})
    return state, signature(state) != signature(previous)


def disposition(state, was_open, exists, changed):
    """Human closures survive repeats; new failure transitions reopen issues."""
    if state['status'] == 'failure' and (changed or not exists):
        return 'open'
    if (state['claim'] == 'availability' or state['claim'] == 'upstream' and all(k.startswith('availability:') for k in state['sources'])) and state['status'] == 'pass':
        return 'closed'
    return 'open' if was_open else 'closed'


def aggregate(states):
    """Shared transport failures by host, keeping all contributing source states.

    Call on the merged persisted + current profile states so a partial profile
    selection cannot incorrectly recover an untested upstream source.
    """
    hosts = {}
    for state in states:
        for source, value in state['sources'].items():
            if value['host']:
                hosts.setdefault(value['host'], {})[state['claim'] + ':' + state['profile'] + ':' + source] = value
    return hosts


def upstream_state(host, sources, run_id):
    failing = {key: s for key, s in sources.items() if s['status'] in ('unavailable', 'evidence unavailable', 'fetch failure')}
    profiles = {key.split(':', 2)[1] for key in failing}
    state = empty('upstream/' + host, 'upstream')
    state.update(sources=deepcopy(sources), last_run_id=run_id, status='failure' if len(profiles) >= 2 else 'pending' if any(s['status'] not in PASSES for s in sources.values()) else 'pass')
    return state


def body(state):
    lines = ['Automated profile maintenance; `updated` remains the last human review date.', '',
             'Status: **' + state['status'] + '**. Claim: **' + state['claim'] + '**.', '',
             'Availability measures obtainability; reproduction measures byte equality; evidence may be deployment or listing information.']
    if state['manual_review']:
        lines += ['', '**Manual review required:** a source was unavailable for four consecutive weekly windows.']
    for source, value in sorted(state['sources'].items()):
        # JSON encoding prevents fetched markup from becoming active issue text.
        lines += ['', '```json', json.dumps({'source': source, 'kind': value['kind'], 'status': value['status'],
                                  'consecutive_failures': value['failures'], 'unavailable_windows': value['unavailable_windows'],
                                  'last_success': value['last_success'], 'deadline': value['deadline'], 'detail': value['detail']}, ensure_ascii=True).replace('<', '\\u003c').replace('>', '\\u003e'), '```']
    lines += ['', '<!-- ' + MARKER, json.dumps(state, ensure_ascii=True, separators=(',', ':')).replace('<', '\\u003c').replace('>', '\\u003e'), '-->']
    return '\n'.join(lines) + '\n'
