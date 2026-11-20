# SPDX-License-Identifier: GPL-3.0-or-later
"""CI-only per-header -dM snapshots and installed-header dependency evidence."""
from functools import cache
import json
from pathlib import Path
import re
import subprocess
import sys
from time import perf_counter

from audit import ROOT, VERSION, classify, apply_trust
from probe import build_inputs, trusted_build_inputs
from member_inventory import inventory
sys.path.append(str(ROOT / 'ci/extensions'))
sys.path.append(str(ROOT / 'ci/profiles'))
from generate import reachable
from scan import search_paths


def command(compiler, argv, source='', check=True):
    return subprocess.run([str(compiler), *argv], input=source, text=True,
                          encoding='utf-8', errors='replace', capture_output=True,
                          check=check, timeout=30)


def dump(text):
    # GCC 14+ can expose __cpp_lib_x as an alias of __glibcxx_x. Resolve only
    # complete object-like identifier chains ending in an integer; retain
    # unfamiliar replacements for evidence and let the audit reject them.
    definitions = dict(re.findall(r'^#define ([A-Za-z_][A-Za-z_0-9]*) (.*)$', text, re.M))
    def value(body, seen=frozenset()):
        if body in definitions and body not in seen:
            return value(definitions[body], seen | {body})
        return body
    result = {}
    for line in text.splitlines():
        m = re.fullmatch(r'#define (__cpp_lib_[A-Za-z_0-9]+) (.*)', line)
        if m:
            if m[1] in result:
                raise ValueError('duplicate feature macro: ' + m[1])
            result[m[1]] = value(m[2])
        elif line.startswith('#define __cpp_lib_'):
            raise ValueError('non-object-like feature macro: ' + line)
    return dict(sorted(result.items()))


def header_names(search):
    names = {line for line in (ROOT / 'data/names/standard-headers.txt').read_text().splitlines()
             if line and not line.startswith('#')}
    for directory in search:
        pbds = directory / 'ext/pb_ds'
        if pbds.is_dir():
            names.update(p.relative_to(directory).as_posix() for p in pbds.rglob('*') if p.is_file())
    return sorted(names)


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, sort_keys=True, indent=2) + '\n', encoding='utf-8', newline='\n')


def capture(compiler, std, flags, build, output, *, search=None, reached=None, provenance=None,
            reproduced=False):
    started = perf_counter()
    argv = ['-x', 'c++', '-std=' + std, *flags]
    version = command(compiler, ['-dumpfullversion', '-dumpversion']).stdout.strip()
    if not re.fullmatch(r'[0-9]+(?:\.[0-9]+)+', version):
        raise ValueError('not a GCC version: ' + version)
    if search is None:
        search = search_paths(command(compiler, [*argv, '-E', '-v', '-']).stderr)
    reached = reachable(search, with_spellings=True) if reached is None else reached
    # Include libc and retain every search-relative spelling, even when its
    # symlink target lies outside the search paths. Include edges use these keys.
    @cache
    def contents(file):
        return file.read_bytes().decode('utf-8', errors='replace')

    files = {name: contents(file)
             for name, file in sorted(reached.items())}
    # -dD provenance distinguishes real compiler predefines from stdc-predef.h
    # and command definitions. Special builtin tests remain unrecognised.
    trace = command(compiler, [*argv, '-dD', '-E', '-']).stdout
    origin, predefines, overridden = '', {}, set()
    for line in trace.splitlines():
        marker = re.match(r'# \d+ "([^"]+)"', line)
        if marker:
            origin = marker[1]
        else:
            m = re.match(r'#(define|undef) ([A-Za-z_][A-Za-z_0-9]*)\b', line)
            if m:
                if m[1] == 'define' and origin == '<built-in>':
                    predefines[m[2]] = line
                else:
                    predefines.pop(m[2], None)
                    overridden.add(m[2])
    # Native-model names must not become host facts indirectly through headers.
    volatile = set()
    if any(f in ('-march=native', '-mtune=native', '-mcpu=native') for f in flags):
        model = ROOT / ('data/native/gcc-' + '.'.join(version.split('.')[:2]) + '-x86.txt')
        if not model.exists():
            raise ValueError('missing native model for library capture')
        volatile = {s.split()[0] for s in model.read_text().splitlines() if s and not s.startswith('#')}
    discovered = perf_counter()
    names = header_names(search)
    audit = classify(files, {n: v for n, v in predefines.items() if n not in volatile},
                     overridden | volatile, roots=names)
    audited = perf_counter()
    print(f'library audit: {audited - discovered:.2f}s ({len(files)} header keys, '
          f'{len(names)} roots)', file=sys.stderr, flush=True)
    observations = build_inputs(compiler, std, flags, search, audit, command)
    observed = perf_counter()
    headers, probes = {}, 0
    disabled = any(f.startswith(('-include', '-imacros')) for f in flags)
    for header in names:
        if disabled:
            headers[header] = dict(status='disabled', macros={})
        elif not any((p / header).is_file() for p in search):
            headers[header] = dict(status='missing', macros={})
        else:
            probes += 1
            process = command(compiler, [*argv, '-dM', '-E', '-'], '#include <' + header + '>\n', check=False)
            headers[header] = dict(status='ok' if process.returncode == 0 else 'error',
                                   macros=dump(process.stdout) if process.returncode == 0 else {})
    preprocessed = perf_counter()
    data = dict(schema=2, generator=VERSION, build=build, gcc=version,
                major=version.split('.')[0], std=std, flags=flags,
                triple=command(compiler, ['-dumpmachine']).stdout.strip(),
                compiler=command(compiler, ['--version']).stdout.splitlines()[0],
                provenance=provenance, reproduced=reproduced, observations=observations,
                volatile=sorted(volatile), headers=headers, audit=audit,
                member_inventory=inventory(files))
    # A raw report describes its own build. The merger reapplies the rules over
    # the complete GCC-major matrix before licensing approximate-profile facts.
    apply_trust(audit, trusted_build_inputs(data, [data]))
    write(output, data)
    print(f'A12 member inventory: {len(data["member_inventory"]["names"])} names, '
          f'token pasting={data["member_inventory"]["token_pasting"]}',
          file=sys.stderr, flush=True)
    finished = perf_counter()
    print(f'library capture: discovery={discovered - started:.2f}s '
          f'audit={audited - discovered:.2f}s '
          f'inputs={observed - audited:.2f}s '
          f'preprocessing={preprocessed - observed:.2f}s '
          f'write={finished - preprocessed:.2f}s total={finished - started:.2f}s '
          f'({len(files)} header keys, {probes} -dM probes)', file=sys.stderr, flush=True)
    return output


def profile_capture(profile_file, compiler, *, reproduced=False):
    import tomllib
    from provenance import identity
    profile = tomllib.loads(profile_file.read_text(encoding='utf-8-sig'))
    return capture(compiler, profile['std'], profile['flags'], 'profile-' + profile['name'],
                   profile_file.with_name(profile['name'] + '-library.json'),
                   reproduced=reproduced,
                   provenance=dict(build=identity(profile, 'build'),
                                   snapshot_compiler=identity(profile, 'snapshot_compiler')))
