# SPDX-License-Identifier: GPL-3.0-or-later
"""CI-only expected diagnostic output, immediately after snapshot capture.

Use the snapshot compiler's exact -x c++ -std=<std> <flags> preprocessing
command. Link-only profile options are excluded, just as in snapshot.py;
this program needs only the compiler's C runtime, not judge-installed libraries.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import tomllib
from flags import operations


def run(argv, **kwargs):
    return subprocess.run([str(a) for a in argv], check=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=120, **kwargs).stdout


def capture(profile_file, compiler, distill):
    profile_file = profile_file.resolve()
    profile = tomllib.loads(profile_file.read_text(encoding='utf-8-sig'))
    operations(profile['flags'], profile.get('inert', []))
    distill = distill.resolve()
    # Probe manifests/digests come from the just-written snapshot, via the
    # explicit profile path, rather than any older snapshot embedded in distill.
    with tempfile.TemporaryDirectory(prefix='.profile-probe-', dir=profile_file.parent) as temp:
        work = Path(temp)
        source, binary = work / 'probe.cpp', work / 'probe.exe'
        # Part membership is deterministic. Find the first total for which ALL
        # parts satisfy BOTH budgets; never infer that from the first part alone.
        sources = None
        for total in range(1, 10001):
            candidate = []
            try:
                for part in range(1, total + 1):
                    candidate.append(run([distill, '--judge-probe', profile_file,
                                          '--part', f'{part}/{total}']))
            except subprocess.CalledProcessError as error:
                if b'probe exceeds source/output byte budget' not in error.stderr:
                    raise
                continue
            sources = candidate
            break
        if sources is None:
            raise ValueError('no probe partition fits profile budgets')
        outputs = []
        for part, contents in enumerate(sources, 1):
            source.write_bytes(contents)
            run([compiler, '-x', 'c++', '-std=' + profile['std'], *profile['flags'],
                 source, '-o', binary])
            output = run([binary])
            if len(output) > profile.get('probe_output_bytes', 60000):
                raise ValueError('probe exceeded output budget')
            if b'!probe-failure:' in output or b'2170726f62652d6661696c7572653a' in output:
                raise ValueError('probe failed while capturing expected output')
            outputs.append(output)
        destination = profile_file.with_name(profile['name'] + '-probe-expected.txt')
        destination.write_bytes(b''.join(outputs))
        # Reloads the expectation just produced and exercises the complete
        # header/trailer/parts checker before uploading it as review evidence.
        run([distill, '--judge-check', profile_file, destination])
        return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('profile', type=Path)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--distill', type=Path, required=True)
    parser.add_argument('--build-distill', action='store_true', help='CI: build the LLVM-free diagnostic CLI first')
    args = parser.parse_args()
    try:
        if args.build_distill:
            build = args.distill.resolve().parent
            subprocess.run(['cmake', '-S', '.', '-B', str(build), '-G', 'Ninja',
                            '-DCMAKE_BUILD_TYPE=Release', '-DDISTILL_PROFILE_ONLY=ON',
                            '-DBUILD_TESTING=OFF',
                            *(['-DCMAKE_CXX_COMPILER=' + args.compiler] if os.name == 'nt' else [])], check=True, timeout=120)
            subprocess.run(['cmake', '--build', str(build), '--target', 'distill',
                            '--parallel', '2'], check=True, timeout=900)
            subprocess.run([sys.executable, 'ci/profiles/test_probe_cli.py',
                            str(args.distill.resolve()), 'tests/unit/fixtures/profile'],
                           check=True, timeout=60)
        print(capture(args.profile, args.compiler, args.distill))
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        detail = getattr(error, 'stderr', b'') or b''
        parser.exit(1, f'expected probe failed: {error}\n{detail.decode(errors="replace")}\n')


if __name__ == '__main__':
    main()
