# SPDX-License-Identifier: GPL-3.0-or-later
"""Capture source-entry macro facts under the exact profile argv (Python 3.11+)."""
import argparse
from itertools import product
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import tomllib
from flags import operations
from provenance import identity

ROOT = Path(__file__).resolve().parents[2]
NAME = r'[A-Za-z_][A-Za-z_0-9]*'
DEFINE = re.compile(r'#define (' + NAME + r')(\([^\n]*?\))?(?: (.*))?\Z')


def run(compiler, *args):
    return subprocess.run([compiler, *args], check=True, text=True, encoding='utf-8', errors='replace',
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30).stdout


def compatible_triple(expected, actual):
    def norm(t):
        p = t.split('-')
        return p[:1] + p[2:] if len(p) >= 4 or (len(p) == 3 and p[-1] == 'mingw32') else p
    return norm(expected) == norm(actual)


def inventory(text):
    result = {}
    for line in text.splitlines():
        m = DEFINE.fullmatch(line)
        if not m or m[1] in result:
            raise ValueError('malformed or duplicate compiler definition: ' + line)
        # Normalize whitespace only at the separation between name and body.
        result[m[1]] = '#define ' + m[1] + (m[2] or '') + ' ' + (m[3] or '')
    if not result:
        raise ValueError('empty macro inventory')
    return result


def startup_origins(trace, empty):
    current, origins = '', {}
    for line in trace.splitlines():
        marker = re.fullmatch(r'# \d+ "((?:\\.|[^"\\])*)"(?: \d+)*', line)
        if marker:
            current = marker[1].replace('\\\\', '/')
            if current not in ('<built-in>', '<command-line>', str(empty).replace('\\', '/')):
                if Path(current).name != 'stdc-predef.h':
                    raise ValueError('unexpected startup header: ' + current)
        elif line.startswith('#define '):
            m = DEFINE.fullmatch(line)
            if not m:
                raise ValueError('invalid definition in startup trace')
            origins[m[1]] = 'startup-header' if Path(current).name == 'stdc-predef.h' else 'command-operation' if current == '<command-line>' else 'compiler-builtin'
        elif line.startswith('#undef '):
            origins.pop(line[7:].strip(), None)
    return origins


def data(kind, key):
    path = ROOT / 'data' / kind / (key + '.txt')
    if not path.exists():
        return []
    return [s for s in path.read_text().splitlines() if s and not s.startswith('#')]


def probe(compiler, argv, source, names):
    source.write_text(''.join(f'#ifdef {name}\nDISTILL_PROBE_{i} 1\n#else\nDISTILL_PROBE_{i} 0\n#endif\n' for i, name in enumerate(names)))
    output = run(compiler, *argv, '-E', str(source))
    results = dict((int(i), bit == '1') for i, bit in re.findall(r'^DISTILL_PROBE_(\d+) ([01])$', output, re.M))
    if set(results) != set(range(len(names))):
        raise ValueError('incomplete special-builtin probe')
    return {name: results[i] for i, name in enumerate(names)}


def cpu_values(compiler, option):
    # GCC's diagnostic contains the complete accepted value list. --help=target
    # can truncate/omit aliases, so ask the option parser, in the real ABI.
    result = subprocess.run([compiler, option + '=distill-invalid-cpu', '-x', 'c++', '-E', '-'],
                            input='', text=True, capture_output=True, timeout=30, env=None)
    m = re.search(r'valid arguments to .?' + re.escape(option) + r'=.?.? switch are: ([^\n;]+)', result.stderr)
    if not m:
        raise ValueError('cannot enumerate ' + option + ' values: ' + result.stderr)
    values = sorted(set(m[1].split()) - {'native'})
    if not values or any(not re.fullmatch(r'[a-zA-Z0-9_-]+', v) for v in values):
        raise ValueError('invalid CPU enumeration')
    return values


def join_native(dumps, model, tune_only=False):
    if not dumps:
        raise ValueError('no CPU configurations accepted in this ABI')
    varying = {n for n in set().union(*dumps) if len({d.get(n) for d in dumps}) > 1}
    allowed = {n for n, scope in model.items() if not tune_only or scope == 'tune'}
    outside = varying - allowed
    if outside:
        raise ValueError('native dependency model error: ' + ', '.join(sorted(outside)))
    result = {}
    for name in allowed:
        values = {d.get(name) for d in dumps}
        if values == {None}:
            result[name] = ('undefined', None)
        elif len(values) == 1:
            result[name] = ('known', next(iter(values)))
        elif None in values:
            result[name] = ('unknown', None)
        else:
            result[name] = ('defined-unknown', None)
    return result


def native_runs(compiler, argv, empty, model):
    indices = [i for i, f in enumerate(argv) if f in ('-march=native', '-mtune=native', '-mcpu=native')]
    if not indices:
        return {}, []
    options = sorted({argv[i].split('=')[0] for i in indices})
    alternatives = [cpu_values(compiler, '-mtune' if option == '-mcpu' else option) for option in options]
    dumps, accepted = [], []
    for cpus in product(*alternatives):
        replacements = dict(zip(options, cpus))
        command = [f.split('=')[0] + '=' + replacements[f.split('=')[0]] if i in indices else f for i, f in enumerate(argv)]
        try:
            dump = inventory(run(compiler, *command, '-dM', '-E', str(empty)))
        except subprocess.CalledProcessError as e:
            # GCC lists 32-bit-only CPUs in a 64-bit compiler as well.
            if 'does not support x86-64' in e.stderr or 'CPU you selected does not support' in e.stderr:
                continue
            raise
        dumps.append(dump)
        accepted.append(command)
    return join_native(dumps, model, '-march' not in options), accepted


def capture(profile_file, compiler, reproduced_build=None):
    profile_file = Path(profile_file).resolve()
    profile = tomllib.loads(profile_file.read_text(encoding='utf-8-sig'))
    if profile['compiler']['family'] != 'gcc':
        raise ValueError('only GCC snapshots are supported')
    version = profile['compiler']['version']
    if not re.fullmatch(r'[0-9]+\.[0-9]+(?:\.[0-9]+)?', version):
        raise ValueError('profile version must be major.minor[.patch]')
    if not re.fullmatch(r'(?:c|gnu)\+\+[0-9]{2}', profile['std']):
        raise ValueError('invalid dialect')
    flags = profile['flags']
    ops = dict(operations(flags, profile.get('inert', [])))
    snapshot = profile.get('predefined', '')
    if not snapshot or snapshot in ('.', '..') or any(c in snapshot for c in '/\\:'):
        raise ValueError('predefined must name a snapshot next to the profile')
    resolved = shutil.which(str(compiler))
    if not resolved:
        raise ValueError(f'compiler not found: {compiler}')
    compiler = str(Path(resolved).resolve())
    actual_version = run(compiler, '-dumpfullversion', '-dumpversion').strip()
    if actual_version.split('.')[:len(version.split('.'))] != version.split('.'):
        raise ValueError(f'GCC version mismatch: expected {version}, got {actual_version}')
    triple = run(compiler, '-dumpmachine').strip()
    if not compatible_triple(profile['triple'], triple):
        raise ValueError(f'target triple mismatch: expected {profile["triple"]}, got {triple}')
    banner = run(compiler, '--version').strip().replace('\r', '').replace('\n', '\\n')
    key = 'gcc-' + '.'.join(actual_version.split('.')[:2])
    builtins = data('builtins', key)
    model = dict(line.split() for line in data('native', key + '-x86'))
    argv = ['-x', 'c++', '-std=' + profile['std'], *flags]
    build_id = identity(profile, 'build')
    snapshot_compiler = identity(profile, 'snapshot_compiler')
    if reproduced_build is not None and (build_id == 'none' or reproduced_build != build_id):
        raise ValueError('--reproduced-build must equal profile build provenance')
    if reproduced_build is not None and snapshot_compiler not in ('none', build_id):
        raise ValueError('snapshot compiler is not the judge build')
    host = any(f in ('-march=native', '-mtune=native', '-mcpu=native') for f in flags)
    if host and (not model or not triple.startswith('x86_64-')):
        raise ValueError('unsupported native model/ABI')
    with tempfile.TemporaryDirectory(prefix='.distill-snapshot-', dir=profile_file.parent) as directory:
        empty = Path(directory) / 'empty.cpp'
        empty.write_bytes(b'')
        macros = inventory(run(compiler, *argv, '-dM', '-E', str(empty)))
        origins = startup_origins(run(compiler, *argv, '-dD', '-E', str(empty)), empty)
        probes = probe(compiler, argv, Path(directory) / 'probe.cpp', builtins) if builtins else {}
        joined, accepted = native_runs(compiler, argv, empty, model)
    if '__clang__' in macros:
        raise ValueError('Clang is not GCC')
    # Independently identify the binary; effective -D/-U may override identity macros.
    for name, expected in zip(('__GNUC__', '__GNUC_MINOR__', '__GNUC_PATCHLEVEL__'), actual_version.split('.')):
        if name not in ops and macros.get(name) != '#define ' + name + ' ' + expected:
            raise ValueError('predefined macros do not identify requested GCC: ' + name)
    states = {n: ('known', definition, 'compiler-builtin' if origins.get(n) == 'command-operation' and n not in ops else origins.get(n, 'compiler-builtin')) for n, definition in macros.items()}
    for name, defined in probes.items():
        if name not in macros:
            states[name] = ('defined-unknown' if defined else 'undefined', None, 'special-builtin')
    for name, definition in ops.items():
        # Startup headers run after command operations and can supersede them.
        if origins.get(name) != 'startup-header':
            if (definition is None and name in macros) or (definition is not None and name not in macros):
                raise ValueError('effective command operation mismatch: ' + name)
            # GCC's dump canonicalizes whitespace. Retain the exact operation's
            # replacement text; the line-marker trace ruled out a later header
            # definition, and the effective command establishes this binding.
            states[name] = ('undefined' if definition is None else 'known', definition, 'command-operation')
    for name, (state, definition) in joined.items():
        if name not in ops:
            states[name] = (state, definition, 'native-model')
    fields = [('distill-snapshot', '1'), ('compiler-path', compiler), ('compiler-version', banner),
              ('gcc-version', actual_version), ('dumpmachine', triple), ('profile-triple', profile['triple']),
              ('build', build_id), ('reproduced', 'yes' if reproduced_build is not None else 'no'),
              ('std', profile['std']), ('builtin-registry', key if builtins else 'none'),
              ('native-model', key + '-x86' if host else 'none')]
    if snapshot_compiler != 'none':
        fields.append(('snapshot-compiler', snapshot_compiler))
    lines = ['# SPDX-License-Identifier: GPL-3.0-or-later', *[f'# {k}: {v}' for k, v in fields],
             *['# flag: ' + f for f in flags], '# end-header']
    for name, (state, definition, origin) in sorted(states.items()):
        if state != 'known':
            lines.append(f'# state {name} {state} {origin}')
        else:
            prefix = {'compiler-builtin': '', 'startup-header': '# startup-header stdc-predef.h ',
                      'command-operation': '# command-operation ', 'native-model': '# native-model '}[origin]
            match = DEFINE.fullmatch(definition)
            lines.append(prefix + (definition[:-1] if match and match[3] == '' else definition))
    lines += [f'# probe {n} {"defined" if bit else "undefined"}' for n, bit in sorted(probes.items())]
    destination = profile_file.parent / snapshot
    destination.write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('profile', type=Path)
    parser.add_argument('--compiler', default='g++')
    parser.add_argument('--reproduced-build', help='affirm package/recipe identity, exactly as kind=value in profile')
    args = parser.parse_args()
    try:
        output = capture(args.profile, args.compiler, args.reproduced_build)
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        parser.exit(1, f'snapshot failed: {error}\n')
    print(output)


if __name__ == '__main__':
    main()
