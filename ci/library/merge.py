# SPDX-License-Identifier: GPL-3.0-or-later
"""Regenerate reviewed L1c data from the complete CI matrix; compare, never union old facts."""
import argparse
from difflib import unified_diff
import json
from pathlib import Path
import sys

from audit import ROOT, VERSION
from capture import write


def merge(reports, expected):
    builds = {}
    for report in reports:
        if (report.get('schema') != 1 or report.get('generator') != VERSION or
                report.get('audit', {}).get('generator') != VERSION):
            raise ValueError('unsupported library report version')
        key = report['build']
        if key in builds:
            raise ValueError('duplicate library build: ' + key)
        builds[key] = report
    if set(builds) != set(expected):
        raise ValueError('incomplete library matrix; missing=' + str(sorted(set(expected) - builds.keys())) +
                         '; unexpected=' + str(sorted(builds.keys() - set(expected))))
    versions = {}
    agreement = {}
    for major in sorted({b['major'] for b in builds.values()}, key=int):
        rows = [b for b in builds.values() if b['major'] == major]
        names = sorted(set().union(*(set(b['audit']['macros']) |
                      set().union(*(set(h['macros']) for h in b['headers'].values())) for b in rows)))
        independent, dependent = {}, {}
        for name in names:
            entries = [b['audit']['macros'].get(name) for b in rows]
            good = all(e and e['independent'] for e in entries)
            if good:
                # A literal pair must be audited in every build of this major.
                pairs = set.intersection(*(set(e['values']) for e in entries))
                independent[name] = sorted(pairs)
            else:
                dependent[name] = sorted(set().union(*(set(e['unrecognised']) if e else
                                                {'<missing-definition>'} for e in entries)))
        versions[major] = dict(independent=independent, dependent=dependent,
                               builds=sorted(b['build'] for b in rows))
        # All matrix snapshots, including different dialects/flags, participate.
        # Thus an unrepresented or disagreeing dialect never licenses a fact.
        headers = sorted(set().union(*(set(b['headers']) for b in rows)))
        agreement[major] = {}
        for header in headers:
            defined, absent, unknown = {}, [], {}
            snapshots = [b['headers'].get(header) for b in rows]
            for name in names:
                reason = None
                values = {s['macros'].get(name) for s in snapshots if s and s['status'] == 'ok'}
                if not all(s and s['status'] == 'ok' for s in snapshots):
                    reason = 'unavailable-header'
                elif len(values) != 1:
                    reason = 'cross-build-disagreement'
                elif name not in independent or not independent[name]:
                    reason = 'config-dependent'
                elif values == {None}:
                    # Absence needs all possible definitions independent, not
                    # merely an independent pair that happens to be present.
                    if all(set(b['audit']['macros'][name]['values']) <= set(independent[name]) for b in rows):
                        absent.append(name)
                    else:
                        reason = 'unaudited-absence'
                elif next(iter(values)) in independent[name]:
                    defined[name] = next(iter(values))
                else:
                    reason = 'unaudited-value'
                if reason:
                    unknown[name] = reason
            agreement[major][header] = dict(defined=defined, absent=absent, unknown=unknown)
    return dict(schema=1, generator=VERSION, builds=dict(sorted(builds.items())),
                versions=versions, agreement=agreement)


def expected_builds():
    # Both matrices are the source of truth; adding any row is mandatory here.
    import yaml
    sys.path.insert(0, str(ROOT / 'ci/profiles'))
    from matrix import matrix
    workflow = yaml.safe_load((ROOT / '.github/workflows/extensions.yml').read_text())
    rows = workflow['jobs']['scan']['strategy']['matrix']['include']
    return ['profile-' + r['profile'] for r in matrix()['include']] + [
        'extensions-' + r['label'] for r in rows if r.get('library', True)]


def check(actual, committed):
    actual = actual.encode('utf-8') if isinstance(actual, str) else actual
    old = committed.read_bytes() if committed.exists() else b''
    if old == actual:
        return True
    if old.replace(b'\r\n', b'\n') == actual.replace(b'\r\n', b'\n'):
        print('L1c bytes differ in line endings')
    print(''.join(unified_diff(old.decode('utf-8', errors='backslashreplace').splitlines(True),
                               actual.decode('utf-8', errors='backslashreplace').splitlines(True),
                               fromfile=str(committed), tofile='CI/library.json')), end='')
    return False


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artifacts', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--check', type=Path)
    args = parser.parse_args()
    try:
        reports = [json.loads(p.read_text(encoding='utf-8')) for p in sorted(args.artifacts.rglob('*-library.json'))]
        data = merge(reports, expected_builds())
        write(args.output, data)
        if args.check and not check(args.output.read_bytes(), args.check):
            parser.exit(1, 'L1c data changed: review the library-generated artifact and merge by PR\n')
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f'library merge failed: {error}\n')
