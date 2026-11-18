# SPDX-License-Identifier: GPL-3.0-or-later
"""CI-only per-header -dM snapshots and installed-header dependency evidence."""
import json
from pathlib import Path
import re
import subprocess
import sys

from audit import ROOT, VERSION, classify
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


def capture(compiler, std, flags, build, output, *, search=None, reached=None, provenance=None):
    argv = ['-x', 'c++', '-std=' + std, *flags]
    version = command(compiler, ['-dumpfullversion', '-dumpversion']).stdout.strip()
    if not re.fullmatch(r'[0-9]+(?:\.[0-9]+)+', version):
        raise ValueError('not a GCC version: ' + version)
    if search is None:
        search = search_paths(command(compiler, [*argv, '-E', '-v', '-']).stderr)
    reached = reachable(search) if reached is None else reached
    files = {}
    for file in sorted(reached):
        # Audit the SAME reachable-header closure as A1, including libc. Library
        # request macros and header include conditions can originate there too.
        owner = next((i for i, p in enumerate(search) if file.is_relative_to(p)), None)
        if owner is None:
            raise ValueError('reachable header outside compiler search paths: ' + str(file))
        files[f'{owner}/' + file.relative_to(search[owner]).as_posix()] = file.read_bytes().decode('utf-8', errors='replace')
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
    audit = classify(files, {n: v for n, v in predefines.items() if n not in volatile}, overridden | volatile)
    headers = {}
    disabled = any(f.startswith(('-include', '-imacros')) for f in flags)
    for header in header_names(search):
        if disabled:
            headers[header] = dict(status='disabled', macros={})
        elif not any((p / header).is_file() for p in search):
            headers[header] = dict(status='missing', macros={})
        else:
            process = command(compiler, [*argv, '-dM', '-E', '-'], '#include <' + header + '>\n', check=False)
            headers[header] = dict(status='ok' if process.returncode == 0 else 'error',
                                   macros=dump(process.stdout) if process.returncode == 0 else {})
    data = dict(schema=1, generator=VERSION, build=build, gcc=version,
                major=version.split('.')[0], std=std, flags=flags,
                triple=command(compiler, ['-dumpmachine']).stdout.strip(),
                compiler=command(compiler, ['--version']).stdout.splitlines()[0],
                provenance=provenance, volatile=sorted(volatile), headers=headers, audit=audit)
    write(output, data)
    return output


def profile_capture(profile_file, compiler):
    import tomllib
    from provenance import identity
    profile = tomllib.loads(profile_file.read_text(encoding='utf-8-sig'))
    return capture(compiler, profile['std'], profile['flags'], 'profile-' + profile['name'],
                   profile_file.with_name(profile['name'] + '-library.json'),
                   provenance=dict(build=identity(profile, 'build'),
                                   snapshot_compiler=identity(profile, 'snapshot_compiler')))
