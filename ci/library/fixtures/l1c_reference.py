# SPDX-License-Identifier: GPL-3.0-or-later
"""Frozen pre-optimization L1c implementation for output-equivalence tests.

Keep the recursive audit and spelling traversal unchanged as a test oracle.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.append(str(ROOT / 'ci/extensions'))
from generate import directive_text, includes

VERSION = 1
NAME = r'[A-Za-z_][A-Za-z_0-9]*'
INTEGER = re.compile(r'(?:0[xX][0-9a-fA-F]+|[0-9]+)[uUlL]*\Z')
# Reviewed candidates, NOT unconditional exemptions. Each installed definition,
# undefinition, replacement and controlling condition is verified below. No
# _GLIBCXX_USE_*, _GLIBCXX_HAVE_*, ABI/hosted/thread/locale switch is admitted.
PURE_CANDIDATES = frozenset({'_GLIBCXX_FLOAT_IS_IEEE_BINARY32',
                             '_GLIBCXX_DOUBLE_IS_IEEE_BINARY64'})


def identifiers(expression):
    # Remove pp-numbers before identifiers, so 201703L does not depend on L.
    expression = re.sub(r'\b(?:0[xX][0-9a-fA-F]+|[0-9]+)[uUlL]*\b', '0', expression)
    names = set(re.findall(NAME, expression)) - {'defined'}
    rest = re.sub(NAME, '', expression)
    if re.search(r'[^\s0()!~+*/%<>=&|^?:,\-]', rest):
        names.add('<unparsed-condition>')
    return names


def own_guard(name, conditions):
    if not name.startswith(('__cpp_lib_', '__glibcxx_')):
        return False
    # Also accept an own-ifndef atom combined with request conditions in GCC's
    # generated #if. A value test or positive defined(self) remains unrecognised.
    negative = r'!\s*defined\s*(?:\(\s*' + re.escape(name) + r'\s*\)|' + re.escape(name) + r'\b)'
    return all(name not in identifiers(re.sub(negative, '1', c)) for c in conditions)


def directives(text):
    """Read directives independently of conditional structure."""
    cleaned = directive_text(text)
    # A raw literal can contain newlines that look like directives. Retain
    # quoted include operands, but erase raw literals before line matching.
    cleaned = re.sub(r'(?:u8|u|U|L)?R"(?P<delimiter>[^\s()\\]{0,16})\(.*?\)(?P=delimiter)"',
                     '""', cleaned, flags=re.S)
    lines = cleaned.splitlines()
    result = []
    for line in lines:
        match = re.match(r'^\s*#\s*(' + NAME + r')\b(.*)', line)
        if match:
            result.append((match[1], match[2].strip()))
    return result


def parse(text, file):
    """Keep previous elif tests for else/elif; no expression evaluation."""
    instructions = directives(text)
    guard = None
    if len(instructions) > 1 and instructions[0][0] == 'ifndef':
        candidate = instructions[0][1]
        if instructions[1] == ('define', candidate) or instructions[1] == ('define', candidate + ' 1'):
            # Only whole-file guards, never a configuration #ifndef.
            depth, end, alternative = 0, None, False
            for i, (op, _) in enumerate(instructions):
                if op in ('if', 'ifdef', 'ifndef'):
                    depth += 1
                elif op in ('else', 'elif', 'elifdef', 'elifndef') and depth == 1:
                    alternative = True
                elif op == 'endif':
                    depth -= 1
                    if depth == 0:
                        end = i
                        break
            if end == len(instructions) - 1 and not alternative:
                guard = candidate
    stack, seen_else, writes, includes = [], [], [], []
    for op, body in instructions:
        if op in ('if', 'ifdef', 'ifndef'):
            expression = body if op == 'if' else ('!' if op == 'ifndef' else '') + 'defined(' + body + ')'
            stack.append([expression])
            seen_else.append(False)
        elif op in ('elif', 'elifdef', 'elifndef', 'else'):
            if not stack or seen_else[-1]:
                raise ValueError(file + ': unmatched ' + op)
            if op == 'else':
                seen_else[-1] = True
            else:
                stack[-1].append(body if op == 'elif' else 'defined(' + body + ')')
        elif op == 'endif':
            if not stack:
                raise ValueError(file + ': unmatched endif')
            stack.pop()
            seen_else.pop()
        elif op in ('define', 'undef', 'include', 'include_next'):
            conditions = [c for branch in stack for c in branch]
            deps = set().union(*(identifiers(c) for c in conditions)) if conditions else set()
            if op in ('define', 'undef'):
                match = re.fullmatch('(' + NAME + r')(.*)', body)
                if not match or (op == 'undef' and match[2].strip()):
                    raise ValueError(file + ': malformed macro write')
                name, value = match.groups()
                writes.append(dict(name=name, value=value.strip() if op == 'define' else None,
                                   dependencies=sorted(deps - {name} if own_guard(name, conditions) else deps),
                                   conditions=conditions, file=file))
            else:
                includes.append((op, body, deps, conditions))
    if stack:
        raise ValueError(file + ': unterminated conditional')
    return writes, includes, guard


def classify(files, predefines, blocked=frozenset()):
    """files maps stable search-directory-relative paths to installed contents.

    Include-edge conditions and request-macro writes participate transitively.
    Unknown computed includes and potentially relevant unparsed files taint
    every definition. This is intentionally stricter than parsing version.def
    or matching macro prefixes.
    """
    parsed, unparsed, unparsed_names = {}, {}, set()
    for file, text in sorted(files.items()):
        try:
            parsed[file] = parse(text, file)
        except ValueError as error:
            unparsed[file] = str(error)
            unparsed_names.update(re.findall(r'\b__cpp_lib_[A-Za-z_0-9]+\b', text))
            # Discard partial writes and guard claims. Recover only include
            # operands, marking their unknown conditions so every reachable
            # header inherits the unparsed source dependency.
            parsed[file] = ([], [(op, body, {'<unparsed-file:' + file + '>'}, None)
                                for op, body in directives(text)
                                if op in ('include', 'include_next')], None)
    all_writes = [w for ws, _, _ in parsed.values() for w in ws]
    writes = {}
    for w in all_writes:
        writes.setdefault(w['name'], []).append(w)
    candidates = {g for _, _, g in parsed.values() if g}
    # A guard-looking use cannot exempt a build switch defined elsewhere.
    guards = {g for g in candidates if all(w['value'] in ('', '1') and
              parsed[w['file']][2] == g for w in writes.get(g, []))}
    inbound = {f: set() for f in files}
    include_evidence, edges, computed = [], [], set()
    by_name = {}
    for file in files:
        by_name.setdefault(file.split('/', 1)[-1], []).append(file)
    for file, (_, includes, _) in parsed.items():
        for kind, operand, deps, conditions in includes:
            include_evidence.append(dict(file=file, kind=kind, operand=operand, dependencies=sorted(deps), conditions=conditions))
            match = re.fullmatch(r'[<"]([^>"\n]+)[>"]', operand)
            if not match:
                if file not in unparsed:
                    computed.update(deps | {'<computed-include>'})
                continue
            local = str(Path(file).parent / match[1]).replace('\\', '/')
            targets = set(by_name.get(match[1], [])) | ({local} if local in files else set())
            for target in targets - {file}:
                inbound[target].update(deps - guards)
                edges.append((file, target))
    included = {target for _, target in edges}
    unknown = computed | {'<unparsed-file:' + file + '>' for file in unparsed
                          if file in included or '__cpp_lib_' in files[file]
                          or '__glibcxx_' in files[file]}
    # Propagate ancestry to a fixed point; edges are indexed once, not found by
    # repeatedly comparing every include with every reachable header.
    changed = True
    while changed:
        changed = False
        for file, target in edges:
            before = len(inbound[target])
            inbound[target].update(inbound[file])
            changed |= len(inbound[target]) != before
    compiler = (set(predefines) | {'__cplusplus', '__STRICT_ANSI__'}) - writes.keys() - set(blocked)

    def plumbing(name):
        return (name.startswith('__glibcxx_want_') or name.startswith('__glibcxx_') and
                '__cpp_lib_' + name[10:] in writes)

    def values(name, seen=frozenset()):
        if name in seen:
            return set()
        result = set()
        for entry in writes.get(name, []):
            body = entry['value']
            if body is not None and INTEGER.fullmatch(body):
                result.add(body)
            elif body and plumbing(body):
                result.update(values(body, seen | {name}))
        return result

    def dependencies(name, seen=frozenset()):
        if name in compiler or name in guards:
            return set()
        if name in seen:
            return {name}  # cycles outside guards are unrecognised
        if name not in PURE_CANDIDATES and not plumbing(name):
            return {name}
        entries = writes.get(name, [])
        if not entries:
            return {name}
        bad = set()
        for w in entries:
            if w['value'] is not None and not (INTEGER.fullmatch(w['value']) or
                    (name.startswith('__glibcxx_want_') and w['value'] == '')):
                if plumbing(w['value']):
                    bad.update(dependencies(w['value'], seen | {name}))
                else:
                    bad.add(name)
            for dep in set(w['dependencies']) | inbound[w['file']] | unknown:
                bad.update(dependencies(dep, seen | {name}))
        return bad

    result = {}
    for name in sorted(writes.keys() | unparsed_names):
        if not name.startswith('__cpp_lib_'):
            continue
        entries = writes.get(name, [])
        bad, deps = set(), set(unknown)
        # Names seen only in unparsed text have no trusted definitions, but
        # still need explicit taint (including for absence agreement).
        for dep in unknown:
            bad.update(dependencies(dep))
        for w in entries:
            direct = set(w['dependencies']) | inbound[w['file']] | unknown
            deps.update(direct)
            for dep in direct:
                bad.update(dependencies(dep))
            if w['value'] is None:
                bad.add('<nonliteral-or-undef>')
            elif not INTEGER.fullmatch(w['value']):
                if plumbing(w['value']) and values(w['value']):
                    deps.add(w['value'])
                    bad.update(dependencies(w['value']))
                else:
                    bad.add('<nonliteral-or-undef>')
        result[name] = dict(values=sorted(values(name)),
                            independent=not bad, dependencies=sorted(deps),
                            unrecognised=sorted(bad), definitions=entries)
    support = {n: entries for n, entries in sorted(writes.items()) if n in PURE_CANDIDATES or n.startswith('__glibcxx_')}
    return dict(generator=VERSION, pure_candidates=sorted(PURE_CANDIDATES), macros=result,
                support=support, includes=include_evidence, unparsed=unparsed,
                compiler_inputs={n: predefines[n] if isinstance(predefines, dict) else None
                                 for n in sorted(set(predefines) & compiler)},
                blocked_inputs=sorted(blocked))


def reachable(search, standard_file=ROOT / "data/names/standard-headers.txt", *, with_spellings=False):
    """Follow all textual includes, active or not, including include_next.

    By default return A1's set of resolved files. With with_spellings, return
    an audit-key -> resolved-file mapping, retaining each search index and
    unresolved relative spelling, including aliases through symlinked dirs.
    """
    search = [Path(p).resolve() for p in search]
    queue, seen, reached = [], set(), {} if with_spellings else set()
    def location(path, index):
        if with_spellings and not path.is_relative_to(search[index]):
            # An absolute quoted operand can name another search directory.
            index = next((i for i, directory in enumerate(search)
                          if path.is_relative_to(directory)), None)
            if index is None:
                raise ValueError('reachable header outside compiler search paths: ' + str(path))
        return (path if with_spellings else path.resolve(), index)
    def resolve(name, begin=0, local=None, inherited=-1):
        if local is not None and (local / name).is_file():
            return location(local / name, inherited)
        for i in range(begin, len(search)):
            if (search[i] / name).is_file():
                return location(search[i] / name, i)
        return None
    for name in (s.strip() for s in standard_file.read_text().splitlines() if s.strip() and not s.startswith('#')):
        found = resolve(name)
        if found:
            queue.append(found)
    for i, directory in enumerate(search):
        pb = directory / 'ext/pb_ds'
        if pb.is_dir():
            queue.extend(location(p, i) for p in pb.rglob('*') if p.is_file())
    while queue:
        file, index = queue.pop()
        if (file, index) in seen:
            continue
        seen.add((file, index))
        resolved = file.resolve() if with_spellings else file
        if with_spellings:
            reached[f'{index}/' + file.relative_to(search[index]).as_posix()] = resolved
        else:
            reached.add(file)
        for kind, quoted, name in includes(resolved.read_bytes().decode('utf-8', errors='replace')):
            found = resolve(name, index + 1 if kind == 'include_next' else 0,
                            file.parent if quoted and kind == 'include' else None, index)
            if found:
                queue.append(found)
    return reached



import json
import subprocess
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
    reached = reachable(search, with_spellings=True) if reached is None else reached
    # Include libc and retain every search-relative spelling, even when its
    # symlink target lies outside the search paths. Include edges use these keys.
    files = {name: file.read_bytes().decode('utf-8', errors='replace')
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
