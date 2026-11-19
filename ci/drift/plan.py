# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate maintenance selection, shared by workflow planning and reporting."""
import argparse
import json
import os
from common import CLAIMS, profiles


def claims(selection):
    selected = list(CLAIMS) if selection == 'all' else selection.split(',')
    if not selected or any(c not in CLAIMS for c in selected) or len(set(selected)) != len(selected):
        raise ValueError('claims must be all or comma-separated evidence,availability,reproduction,library')
    return selected


def build_parser():
    return argparse.ArgumentParser(description=__doc__)


if __name__ == '__main__':
    build_parser().parse_args()
    event = os.environ['EVENT_NAME']
    selected = claims(os.environ.get('CLAIMS', 'all')) if event == 'workflow_dispatch' else (['reproduction'] if os.environ.get('SCHEDULE') == '23 6 1 * *' else ['evidence', 'availability', 'library'])
    names = profiles(os.environ.get('PROFILES') or 'all')
    with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
        output.write('profiles=' + ','.join(names) + '\n')
        output.write('claims=' + ','.join(selected) + '\n')
        for claim in CLAIMS:
            output.write(claim + '=' + json.dumps(claim in selected) + '\n')
