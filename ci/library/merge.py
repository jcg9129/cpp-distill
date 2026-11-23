# SPDX-License-Identifier: GPL-3.0-or-later
"""Regenerate reviewed L1c data from the complete CI matrix; compare, never union old facts."""
import argparse
from difflib import unified_diff
import hashlib
import json
from pathlib import Path
import sys
import tempfile
from time import perf_counter

from audit import ROOT, VERSION, apply_trust, root_macros
from probe import trusted_build_inputs
from member_inventory import validate_inventory
from report_store import ReportStore, report_digest, write_evidence


def canonical(data):
    return json.dumps(data, sort_keys=True, separators=(',', ':'), ensure_ascii=True)


def digest(data):
    return hashlib.sha256(canonical(data).encode('utf-8')).hexdigest()


def merge(reports, expected, progress=None):
    builds = {}
    for report in reports:
        if (report.get('schema') != 2 or report.get('generator') != VERSION or
                report.get('audit', {}).get('generator') != VERSION):
            raise ValueError('unsupported library report version')
        if not validate_inventory(report.get('member_inventory')):
            raise ValueError('missing or malformed library member inventory')
        key = report['build']
        if key in builds:
            raise ValueError('duplicate library build: ' + key)
        builds[key] = report
    if set(builds) != set(expected):
        raise ValueError('incomplete library matrix; missing=' + str(sorted(set(expected) - builds.keys())) +
                         '; unexpected=' + str(sorted(builds.keys() - set(expected))))

    # Raw captures can justify only their own inputs. Revoke those assumptions
    # unless every scanned build of the major supplies the required evidence.
    # Mutate the reports so the full evidence and its compact digests describe
    # exactly the classifications used below; requirements remain unchanged.
    for major in {b['major'] for b in builds.values()}:
        rows = [b for b in builds.values() if b['major'] == major]
        for report in rows:
            apply_trust(report['audit'], trusted_build_inputs(report, rows))

    # Content-address whole repeated records, rather than repeating the same
    # snapshots/classifications/absence lists hundreds of times. References are
    # stable across unrelated additions; detect even a truncated-hash collision.
    tables = dict(snapshots={}, classifications={}, agreements={}, macro_classifications={},
                  macro_sets={}, build_headers={}, root_classifications={}, root_agreements={})
    def intern(table, entry):
        key = digest(entry)[:16]
        if key in tables[table] and tables[table][key] != entry:
            raise ValueError('library record digest collision')
        tables[table][key] = entry
        return key

    # Embedding uses a global name universe, including names seen only in a
    # different GCC major. Its own native-dependent absences need blockers too.
    universe = {n for b in builds.values() for n in b['audit']['macros']}
    universe.update(n for b in builds.values() for s in b['headers'].values() for n in s['macros'])
    versions, agreement = {}, {}
    for major in sorted({b['major'] for b in builds.values()}, key=int):
        rows = [builds[k] for k in sorted(builds) if builds[k]['major'] == major]
        names = sorted(set().union(*(set(b['audit']['macros']) |
                      set().union(*(set(h['macros']) for h in b['headers'].values())) for b in rows)))
        headers = sorted(set().union(*(set(b['headers']) for b in rows)))
        versions[major] = dict(builds=[b['build'] for b in rows], roots={})
        agreement[major] = {}
        for header in headers:
            roots = [b['audit']['roots'].get(header, dict(macros={}, taint=['<missing-root>'])) for b in rows]
            for i, (build, root) in enumerate(zip(rows, roots)):
                # A non-candidate in this capture can be an integer candidate
                # in another build. Unanalysed writes never establish absence;
                # all native inputs conservatively block own absence as well.
                if root.get('noninteger') or 'root_records' in build['audit']:
                    roots[i] = root = dict(root, macros=root_macros(build['audit'], root))
                for name in universe.intersection(root.get('noninteger', [])):
                    root['macros'][name] = dict(independent=False, pairs={},
                        dependencies=build['volatile'], unrecognised=['<noninteger-candidate>'])
            taint = sorted(set().union(*(set(r['taint']) for r in roots)))
            default = dict(independent=not taint, values=[], dependencies=taint)
            classification = {}
            for name in sorted(set().union(*(set(r['macros']) for r in roots))):
                entries = [r['macros'].get(name) for r in roots]
                # Missing definitions in a complete root closure prove absence,
                # but cannot license a positive value in that build.
                pairs = [set(v for v, p in e['pairs'].items() if p['independent']) if e else set()
                         for e in entries]
                classification[name] = dict(
                    independent=not taint and all(not e or e['independent'] for e in entries),
                    values=sorted(set.intersection(*pairs)) if not taint else [],
                    dependencies=sorted(set(taint).union(*(set(e['dependencies']) | set(e['unrecognised'])
                                                          for e in entries if e))))
            common = set.intersection(*(set(e['dependencies']) for e in classification.values())) if classification else set()
            stored = {n: dict(e, dependencies=sorted(set(e['dependencies']) - common))
                      for n, e in classification.items()}
            versions[major]['roots'][header] = intern('classifications', dict(
                default=default, dependencies=sorted(common),
                macros=intern('macro_sets', {n: intern('macro_classifications', e) for n, e in stored.items()})))
            defined, absent, unknown = {}, [], {}
            snapshots = [b['headers'].get(header) for b in rows]
            unavailable = not all(s and s['status'] == 'ok' for s in snapshots)
            functions = set().union(*(s.get('function_like', []) for s in snapshots if s))
            nonintegers = set().union(*(s.get('noninteger', []) for s in snapshots if s))
            for name in names:
                entry = classification.get(name, default)
                values = {s['macros'].get(name) for s in snapshots if s and s['status'] == 'ok'}
                reason = None
                if unavailable:
                    reason = 'unavailable-header'
                elif name in functions:
                    reason = 'function-like-definition'
                elif name in nonintegers:
                    reason = 'noninteger-definition'
                elif len(values) != 1:
                    reason = 'cross-build-disagreement'
                elif values == {None}:
                    if entry['independent']:
                        absent.append(name)
                    else:
                        reason = 'config-dependent'
                elif next(iter(values)) in entry['values']:
                    defined[name] = next(iter(values))
                else:
                    reason = 'config-dependent' if not entry['independent'] else 'unaudited-value'
                if reason:
                    unknown[name] = reason
            agreement[major][header] = intern('agreements', dict(defined=defined, absent=absent, unknown=unknown))
        if progress:
            progress('Classified GCC ' + major)
    for major in versions:
        versions[major]['roots'] = intern('root_classifications', versions[major]['roots'])
        agreement[major] = intern('root_agreements', agreement[major])
    compact_builds = {}
    for key, build in sorted(builds.items()):
        compact_builds[key] = {k: v for k, v in build.items() if k not in ('audit', 'headers', 'member_inventory')}
        inv = build['member_inventory']
        compact_builds[key]['member_inventory'] = dict(
            names_sha256=digest(inv['names']), name_count=len(inv['names']),
            reachable_headers=inv['reachable_headers'], token_pasting=inv['token_pasting'])
        compact_builds[key].update(evidence_sha256=report_digest(build),
                                  headers=intern('build_headers', {h: intern('snapshots', s) for h, s in sorted(build['headers'].items())}))
        if progress:
            progress('Hashed evidence for ' + key)
    if progress:
        progress(f'Compacting {len(tables["macro_sets"])} macro sets')
    tables['macro_sets'] = delta_sets(tables['macro_sets'])
    member_names = sorted(set().union(*(set(b['member_inventory']['names']) for b in builds.values())))
    return dict(schema=2, generator=VERSION, builds=compact_builds, member_names=member_names,
                versions=versions, agreement=agreement, **tables)


def delta_sets(sets):
    """Share repeated name maps with deterministic, acyclic base-plus-deltas."""
    result, previous = {}, []
    # JSON object size is additive in its encoded key/value pairs. Avoid
    # serializing every candidate in this quadratic search; preserve the exact
    # old cost and tie-breaking (including optional removed lists).
    names = {n for entries in sets.values() for n in entries}
    weights = {n: len(canonical(n)) for n in names}
    pairs = {item for entries in sets.values() for item in entries.items()}
    pair_sizes = {(n, v): weights[n] + 1 + len(canonical(v)) for n, v in pairs}
    def map_size(items):
        return 2 + sum(map(pair_sizes.__getitem__, items)) + len(items) - bool(items)
    for key, entries in sorted(sets.items(), key=lambda kv: (len(kv[1]), canonical(kv[1]))):
        best = dict(entries=entries)
        items = set(entries.items())
        cost = 12 + map_size(items)  # {"entries":...}
        for base, old, old_items, overhead in previous:
            changes = items - old_items
            removed = sorted(old.keys() - entries.keys())
            size = overhead + map_size(changes)
            if removed:
                size += 12 + sum(map(weights.__getitem__, removed)) + len(removed)
            if size < cost:
                best, cost = dict(base=base, entries=dict(changes)), size
                if removed:
                    best['removed'] = removed
        result[key] = best
        previous.append((key, entries, items, 20 + len(canonical(key))))
    return result


def get_snapshot(data, build, header):
    return data['snapshots'][data['build_headers'][data['builds'][build]['headers']][header]]


def get_agreement(data, major, header):
    return data['agreements'][data['root_agreements'][data['agreement'][major]][header]]


def get_classification(data, major, header):
    root = data['classifications'][data['root_classifications'][data['versions'][major]['roots']][header]]
    def entries(key):
        chain = []
        while True:
            record = data['macro_sets'][key]
            chain.append(record)
            if 'base' not in record:
                break
            key = record['base']
        result = {}
        for record in reversed(chain):
            for name in record.get('removed', []):
                del result[name]
            result.update(record['entries'])
        return result
    macros = {}
    for name, key in entries(root['macros']).items():
        entry = data['macro_classifications'][key]
        macros[name] = dict(entry, dependencies=sorted(set(root['dependencies']) | set(entry['dependencies'])))
    return dict(default=root['default'], macros=macros)


def baseline_text(data):
    """Sorted JSON with one record per line, including content-address tables."""
    sections = []
    for key, value in sorted(data.items()):
        if isinstance(value, dict):
            entries = [f'    {json.dumps(k)}: {canonical(v)}' for k, v in sorted(value.items())]
            sections.append(f'  {json.dumps(key)}: {{\n' + ',\n'.join(entries) + '\n  }')
        else:
            sections.append(f'  {json.dumps(key)}: {canonical(value)}')
    return '{\n' + ',\n'.join(sections) + '\n}\n'


def inventory_bytes(data):
    """Exact compact JSON bytes attributable to the A12 fields."""
    without = {k: v for k, v in data.items() if k != 'member_names'}
    without['builds'] = {k: {f: v for f, v in b.items() if f != 'member_inventory'}
                         for k, b in data['builds'].items()}
    return len(baseline_text(data).encode()) - len(baseline_text(without).encode())


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
    sys.stdout.writelines(unified_diff(old.decode('utf-8', errors='backslashreplace').splitlines(True),
                               actual.decode('utf-8', errors='backslashreplace').splitlines(True),
                               fromfile=str(committed), tofile='CI/library.json'))
    return False


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artifacts', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--check', type=Path)
    parser.add_argument('--evidence-output', type=Path, help='full artifact (default: beside --output)')
    parser.add_argument('--allow-subset', action='store_true',
                        help='benchmark/test only: accept an incomplete matrix; never use for reviewed data')
    args = parser.parse_args()
    if args.allow_subset and args.check:
        parser.error('--allow-subset cannot be used with --check')
    try:
        started = perf_counter()
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='library-merge-', dir=args.output.parent) as scratch:
            store = ReportStore(Path(scratch) / 'roots.sqlite')
            try:
                reports = []
                for i, path in enumerate(sorted(args.artifacts.rglob('*-library.json'))):
                    reports.append(store.load(path, i))
                    print(f'Loaded {reports[-1]["build"]}: {perf_counter() - started:.1f}s', flush=True)
                data = merge(reports, [r['build'] for r in reports] if args.allow_subset else expected_builds(),
                             progress=lambda message: print(f'{message}: {perf_counter() - started:.1f}s', flush=True))
                print(f'Merged {len(reports)} builds: {perf_counter() - started:.1f}s', flush=True)
                write_evidence(args.evidence_output or args.output.with_name('library-evidence.json'), reports, VERSION)
            finally:
                store.close()
        print(f'A12 member inventory: {len(data["member_names"])} names, '
              f'{len(data["builds"])} scanned builds, {inventory_bytes(data)} added compact JSON bytes')
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(baseline_text(data), encoding='utf-8', newline='\n')
        if args.check and not check(args.output.read_bytes(), args.check):
            parser.exit(1, 'L1c data changed: review the library-generated artifact and merge by PR\n')
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f'library merge failed: {error}\n')
