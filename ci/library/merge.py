# SPDX-License-Identifier: GPL-3.0-or-later
"""Regenerate reviewed L1c data from the complete CI matrix; compare, never union old facts."""
import argparse
from difflib import unified_diff
from pathlib import Path
import re
import sys
import tempfile
from time import perf_counter

from audit import ROOT, VERSION, apply_trust, root_macros
from probe import trusted_build_inputs
from member_inventory import validate_inventory
from report_store import ReportStore, report_digest, write_evidence
from baseline import canonical, digest, delta_sets, baseline_text, decode


def harmless_flag(flag):
    # Keep aligned with LibraryData::selector. In particular -Wp, can write
    # macros, and the last optimization level changes compiler predefines.
    return (flag.startswith('-W') and not flag.startswith('-Wp,') or
            flag.startswith(('-g', '-fdiagnostics-', '-fconstexpr-depth=',
                             '-fconstexpr-cache-depth=', '-fconstexpr-loop-limit=',
                             '-fconstexpr-ops-limit=')) or flag == '-pipe')


def standard_scope(build):
    triple = build['triple'].split('-')
    if len(triple) >= 4 or len(triple) == 3 and triple[-1] == 'mingw32':
        del triple[1]
    # Keep aligned with LibraryData::selector: retain only the last -O level
    # at its own position. Explicit -f options remain relevant in any order.
    flags = []
    seen_optimization = False
    for flag in reversed(build['flags']):
        if re.fullmatch(r'-O(?:[0-9]+|s|fast|g|z)?', flag):
            if seen_optimization:
                continue
            seen_optimization = True
        if not harmless_flag(flag):
            flags.append(flag)
    return (build['major'], build['std'], '-'.join(triple),
            tuple(reversed(flags)))


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
    versions, agreement, standard_scopes, scope_dependencies = {}, {}, {}, {}
    groups = {}
    for build in builds.values():
        groups.setdefault(standard_scope(build), []).append(build['build'])
    matrices = {major: sorted(k for k, b in builds.items() if b['major'] == major)
                for major in sorted({b['major'] for b in builds.values()}, key=int)}
    build_scopes = {}
    for scope, keys in sorted(groups.items()):
        keys.sort()
        if keys != matrices[scope[0]]:
            selector = 'gcc-' + scope[0] + '@' + digest(scope)[:16]
            matrices[selector] = keys
            build_scopes.update(dict.fromkeys(keys, selector))
    for matrix, keys in matrices.items():
        rows = [builds[k] for k in keys]
        scoped = matrix.startswith('gcc-')
        name_rows = [builds[k] for k in matrices[rows[0]['major']]] if scoped else rows
        names = sorted(set().union(*(set(b['audit']['macros']) |
                      set().union(*(set(h['macros']) for h in b['headers'].values())) for b in name_rows)))
        if scoped:
            names = [n for n in names if not n.startswith('__cpp_lib_')]
        headers = sorted(set().union(*(set(b['headers']) for b in rows)))
        volatile = set().union(*(set(b.get('volatile', [])) for b in rows))
        versions[matrix] = dict(builds=keys, roots={})
        agreement[matrix] = {}
        scope_dependencies[matrix] = set()
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
                if scoped and name.startswith('__cpp_lib_'):
                    continue
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
            versions[matrix]['roots'][header] = intern('classifications', dict(
                default=default, dependencies=sorted(common),
                macros=intern('macro_sets', {n: intern('macro_classifications', e) for n, e in stored.items()})))
            defined, absent, unknown = {}, [], {}
            # Keep the major-wide observation requirement. A scoped proof can
            # discharge unrelated native/configuration blockers, but must not
            # turn a target/argv value disagreement into a new positive fact.
            snapshots = [b['headers'].get(header) for b in name_rows]
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
                    major_absent = (not scoped or name in tables['agreements'][
                        agreement[rows[0]['major']][header]]['absent'])
                    # Scope refinement licenses positive header values only.
                    # Keep absence/identity claims at their existing major-wide
                    # strength, including every omitted-write blocker.
                    if entry['independent'] and major_absent:
                        absent.append(name)
                    else:
                        reason = 'config-dependent'
                elif next(iter(values)) in entry['values']:
                    # A different reachable write can still replace this pair
                    # on another native host. Its real dependency must block
                    # approximate agreement just as it blocks own snapshots.
                    if not name.startswith('__cpp_lib_') and volatile.intersection(entry['dependencies']):
                        reason = 'config-dependent'
                    else:
                        defined[name] = next(iter(values))
                else:
                    reason = 'config-dependent' if not entry['independent'] else 'unaudited-value'
                if reason:
                    unknown[name] = reason
                elif not name.startswith('__cpp_lib_'):
                    scope_dependencies[matrix].update(entry['dependencies'])
            agreement[matrix][header] = intern('agreements', dict(defined=defined, absent=absent, unknown=unknown))
        if progress:
            progress('Classified ' + matrix)
    for major in versions:
        versions[major]['roots'] = intern('root_classifications', versions[major]['roots'])
        agreement[major] = intern('root_agreements', agreement[major])
    for selector in build_scopes.values():
        if selector in versions:
            standard_scopes[selector] = dict(versions.pop(selector), agreement=agreement.pop(selector))
    compact_builds = {}
    for key, build in sorted(builds.items()):
        compact_builds[key] = {k: v for k, v in build.items() if k not in ('audit', 'headers', 'member_inventory')}
        inv = build['member_inventory']
        compact_builds[key]['member_inventory'] = dict(
            names_sha256=digest(inv['names']), name_count=len(inv['names']),
            reachable_headers=inv['reachable_headers'], token_pasting=inv['token_pasting'])
        compact_builds[key].update(evidence_sha256=report_digest(build),
                                  headers=intern('build_headers', {h: intern('snapshots', s) for h, s in sorted(build['headers'].items())}))
        if key in build_scopes:
            compact_builds[key]['standard_scope'] = build_scopes[key]
        selector = build_scopes.get(key, build['major'])
        compiler = build['audit'].get('compiler_inputs', {})
        absent = set(build['audit'].get('source_entry_absences', []))
        # GNU dialects do not predefine __STRICT_ANSI__. Like the C version
        # gate, its absence is part of the captured compiler invocation.
        if '__STRICT_ANSI__' not in compiler and '__STRICT_ANSI__' not in build['audit'].get('blocked_inputs', []):
            absent.add('__STRICT_ANSI__')
        compiler_names = set().union(*(set(builds[k]['audit'].get('compiler_inputs', {}))
                                       for k in matrices[selector]))
        # A compiler input used by another capture in this scope must not
        # disappear from the checks merely because this compiler lacks it.
        # Its source-entry absence is a required snapshot fact too.
        inputs = {n: compiler.get(n) for n in sorted(scope_dependencies[selector] & (compiler_names | absent))}
        if inputs or key in build_scopes:
            compact_builds[key]['standard_inputs'] = inputs
        if progress:
            progress('Hashed evidence for ' + key)
    if progress:
        progress(f'Compacting {len(tables["macro_sets"])} macro sets')
    tables['macro_sets'] = delta_sets(tables['macro_sets'])
    member_names = sorted(set().union(*(set(b['member_inventory']['names']) for b in builds.values())))
    return dict(schema=2, generator=VERSION, builds=compact_builds, member_names=member_names,
                versions=versions, agreement=agreement, **tables,
                **(dict(standard_scopes=standard_scopes) if standard_scopes else {}))


def get_snapshot(data, build, header):
    data = decode(data)
    return data['snapshots'][data['build_headers'][data['builds'][build]['headers']][header]]


def get_agreement(data, major, header):
    data = decode(data)
    ref = (data['standard_scopes'][major]['agreement'] if major in data.get('standard_scopes', {})
           else data['agreement'][major])
    return data['agreements'][data['root_agreements'][ref][header]]


def get_classification(data, major, header):
    data = decode(data)
    version = data.get('standard_scopes', {}).get(major, data['versions'].get(major))
    root = data['classifications'][data['root_classifications'][version['roots']][header]]
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
