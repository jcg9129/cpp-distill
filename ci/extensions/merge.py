# SPDX-License-Identifier: GPL-3.0-or-later
"""Union the compatibility jobs and the checked-in lists for review in one PR."""
import argparse
import json
from datetime import date
from pathlib import Path
from generate import LIBRARIES, ROOT, VERSION, read_list, reserved, standard
from identity import registry, validate_report


def merge(artifacts, output):
    policy = registry()
    # Each scanned inventory must carry its A2 evidence; checked-in A1 lists
    # predate this check and are not fresh scan artifacts.
    for library in LIBRARIES:
        for inventory in sorted(artifacts.rglob(library + '.txt')):
            report = json.loads(inventory.with_name(library + '-identity.json').read_text(encoding='utf-8'))
            if report.get('library') != library:
                raise ValueError(f'{inventory}: mismatched A2 identity report')
            validate_report(report, policy)
    output.mkdir(parents=True, exist_ok=True)
    standard_names, _ = read_list(ROOT / 'data/names/standard-macros.txt')
    for library in LIBRARIES:
        paths = [ROOT / 'data/extensions' / (library + '.txt'), *sorted(artifacts.rglob(library + '.txt'))]
        names, provenance = set(), set()
        for path in paths:
            extra, comments = read_list(path)
            if extra and not any('generate.py v2;' in c for c in comments):
                raise ValueError(f'{path}: only v2 reachable-header inventories may be merged')
            names.update(extra)
            provenance.update(c for c in comments if c.startswith('# Scanned:'))
        names = {n for n in names if not reserved(n) and not standard(n, standard_names)}
        header = ['# Roots: data/names/standard-headers.txt and every ext/pb_ds/ header.',
                  '# Limitation: computed include operands are not followed.',
                  '# SPDX-License-Identifier: GPL-3.0-or-later',
                  f'# A1 {library}; generator ci/extensions/generate.py v{VERSION}; merged {date.today().isoformat()}',
                  *sorted(provenance)]
        if not names:
            header.append('# Not yet generated; protection unavailable.')
        (output / (library + '.txt')).write_text('\n'.join(header + sorted(names)) + '\n', encoding='utf-8', newline='\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artifacts', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    merge(args.artifacts, args.output)
