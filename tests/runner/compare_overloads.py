#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Optional differential check against a separately built main executable.

The extraction runner checks physical declaration coverage against opt-out on
every fixture. This checks that opt-out still renders exactly main's bytes,
and independently checks enabled retention by declaration label and provenance.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import subprocess


def families(report):
    result = {}
    for line in report.splitlines():
        match = re.fullmatch(r'family \d+ ("(?:[^"\\]|\\.)*"): (.*); (.*:\d+)', line)
        if match:
            label, reason, location = match.groups()
            key = (json.loads(label), location)
            result[key] = result.get(key, False) or not reason.startswith('removed:')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--main', type=Path, required=True)
    parser.add_argument('--new', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cases', type=Path, default=Path(__file__).resolve().parents[1] / 'cases/prune')
    parser.add_argument('--render', choices=['plain', 'nlm', 'compact'], default='nlm')
    parser.add_argument('--judge')
    parser.add_argument('--case', action='append', default=[], help='Only compare named fixtures (repeatable)')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    binaries = {'main': args.main.resolve(), 'off': args.new.resolve(), 'on': args.new.resolve()}
    render = {'plain': [], 'nlm': ['--no-line-markers'], 'compact': ['--compact-whitespace']}[args.render]
    failures = []
    protections = Counter()
    removed = Counter()
    marker_growth = {}
    rejected = {}
    passed = 0
    for source in sorted(args.cases.glob('*/main.cpp')):
        if args.case and source.parent.name not in args.case:
            continue
        directory = args.output / source.parent.name
        directory.mkdir(exist_ok=True)
        if directory.resolve() == source.parent.resolve():
            parser.error('--output must not overwrite source fixtures')
        outputs, reports, statuses = {}, {}, {}
        for mode, binary in binaries.items():
            output, report = directory / (mode + '.cpp'), directory / (mode + '.report')
            output.unlink(missing_ok=True)
            report.unlink(missing_ok=True)
            command = [str(binary), *render, '--report', str(report.resolve()), '-o', str(output.resolve())]
            if args.judge:
                command += ['--judge', args.judge]
            if mode == 'off':
                command.append('--no-assume=unreferenced-overloads')
            command += ['g++', '-std=gnu++23', str(source.resolve())]
            try:
                run = subprocess.run(command, capture_output=True, timeout=45)
                (directory / (mode + '.log')).write_bytes(run.stdout + run.stderr)
                statuses[mode] = run.returncode
                if not run.returncode and (not output.exists() or not report.exists()):
                    raise RuntimeError(f'{mode} failed: exit {run.returncode}')
                outputs[mode] = output.read_bytes() if output.exists() else None
                reports[mode] = report.read_text() if report.exists() else ''
            except (OSError, subprocess.TimeoutExpired, RuntimeError) as error:
                failures.append(f'{source.parent.name}: {error}')
                break
        if len(outputs) != 3:
            continue
        if any(statuses.values()):
            # Some extraction fixtures deliberately cannot pass the CLI's
            # baseline compiler/flattening gate. Check matching rejections
            # explicitly instead of treating them as successful comparisons.
            if len(set(statuses.values())) != 1 or outputs['main'] != outputs['off'] or outputs['main'] != outputs['on']:
                failures.append(f'{source.parent.name}: rejection differs: {statuses}')
            else:
                rejected[source.parent.name] = statuses['main']
            continue
        if outputs['main'] != outputs['off']:
            failures.append(f'{source.parent.name}: opt-out bytes differ from main')
        old, new = families(reports['main']), families(reports['on'])
        for key, kept in new.items():
            if kept and not old.get(key, False):
                failures.append(f'{source.parent.name}: resurrected {key}')
        old_bytes = re.search(r'unique family bytes kept=(\d+)', reports['main'])
        new_bytes = re.search(r'unique family bytes kept=(\d+)', reports['on'])
        if old_bytes and new_bytes and int(new_bytes[1]) > int(old_bytes[1]):
            failures.append(f'{source.parent.name}: retained declaration bytes grew')
        if len(outputs['on']) > len(outputs['main']):
            if args.render == 'plain':
                # More deletion holes can require additional #line markers.
                # Family retention/bytes and opt-out identity still must pass.
                marker_growth[source.parent.name] = len(outputs['on']) - len(outputs['main'])
            else:
                failures.append(f'{source.parent.name}: enabled output grew')
        for reason, count in re.findall(r'rule1-constructors-kept: reason=("(?:[^"\\]|\\.)*") declarations=(\d+)', reports['on']):
            protections[json.loads(reason)] += int(count)
        for kind, count in re.findall(r'rule1-removed: category=(\S+) declarations=(\d+)', reports['on']):
            removed[kind] += int(count)
        passed += 1
    summary = dict(cases=passed, render=args.render, judge=args.judge, failures=failures,
                   constructor_protections=dict(protections), removed=dict(removed), marker_growth=marker_growth,
                   matching_rejections=rejected)
    (args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2), flush=True)
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
