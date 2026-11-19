# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail-closed textual libstdc++ feature-macro audit, including inactive branches."""
from collections import deque
from functools import lru_cache
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.append(str(ROOT / 'ci/extensions'))
from generate import directive_text

VERSION = 1
NAME = r'[A-Za-z_][A-Za-z_0-9]*'
INTEGER = re.compile(r'(?:0[xX][0-9a-fA-F]+|[0-9]+)[uUlL]*\Z')
# Reviewed candidates, NOT unconditional exemptions. Each installed definition,
# undefinition, replacement and controlling condition is verified below. No
# _GLIBCXX_USE_*, _GLIBCXX_HAVE_*, ABI/hosted/thread/locale switch is admitted.
PURE_CANDIDATES = frozenset({'_GLIBCXX_FLOAT_IS_IEEE_BINARY32',
                             '_GLIBCXX_DOUBLE_IS_IEEE_BINARY64'})


@lru_cache(maxsize=8192)
def identifiers(expression):
    # Remove pp-numbers before identifiers, so 201703L does not depend on L.
    expression = re.sub(r'\b(?:0[xX][0-9a-fA-F]+|[0-9]+)[uUlL]*\b', '0', expression)
    names = set(re.findall(NAME, expression)) - {'defined'}
    rest = re.sub(NAME, '', expression)
    if re.search(r'[^\s0()!~+*/%<>=&|^?:,\-]', rest):
        names.add('<unparsed-condition>')
    return frozenset(names)


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


def propagate(evidence, successors):
    """Union evidence to a fixed point using a queue of changed sources."""
    pending = deque(name for name, values in evidence.items() if values)
    queued = set(pending)
    while pending:
        source = pending.popleft()
        queued.remove(source)
        for target in successors[source]:
            before = len(evidence[target])
            evidence[target].update(evidence[source])
            if len(evidence[target]) != before and target not in queued:
                pending.append(target)
                queued.add(target)


def dependency_expander(edges, seeds):
    """Expand each dependency once, treating all cyclic members as unrecognised.

    Tarjan visits strongly connected components in dependency order. Every
    member shares terminal evidence and, for a cyclic component, all member
    names. Anything depending on a cycle inherits that unrecognised evidence.
    """
    indices, low, stack, active, results = {}, {}, [], set(), {}
    def connect(name):
        indices[name] = low[name] = len(indices)
        stack.append(name)
        active.add(name)
        for dep in edges.get(name, ()):
            if dep not in indices:
                connect(dep)
                low[name] = min(low[name], low[dep])
            elif dep in active:
                low[name] = min(low[name], indices[dep])
        if low[name] != indices[name]:
            return
        members = set()
        while True:
            member = stack.pop()
            active.remove(member)
            members.add(member)
            if member == name:
                break
        common = set()
        for member in members:
            common.update(seeds.get(member, {member}))
            for dep in edges.get(member, ()):
                if dep not in members:
                    common.update(results[dep])
        if len(members) > 1 or name in edges.get(name, ()):
            common.update(members)
        for member in members:
            results[member] = common.copy()

    for name in edges:
        if name not in indices:
            connect(name)
    return lambda name: results.get(name, frozenset({name}))


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
    # Only revisit successors of a file whose inbound evidence has grown.
    successors = {f: set() for f in files}
    for file, target in edges:
        successors[file].add(target)
    propagate(inbound, successors)
    compiler = (set(predefines) | {'__cplusplus', '__STRICT_ANSI__'}) - writes.keys() - set(blocked)

    def plumbing(name):
        return (name.startswith('__glibcxx_want_') or name.startswith('__glibcxx_') and
                '__cpp_lib_' + name[10:] in writes)

    # Precompute each macro's direct evidence once. Multiple writes, aliases
    # and include paths often repeat the same dependency subgraphs.
    value_sets = {name: set() for name in writes}
    value_users = {name: set() for name in writes}
    dependency_edges, dependency_seeds = {}, {}
    for name, entries in writes.items():
        direct, bad = set(), set()
        for w in entries:
            body = w['value']
            if body is not None and INTEGER.fullmatch(body):
                value_sets[name].add(body)
            elif body and plumbing(body) and body in writes:
                value_users[body].add(name)
            if body is not None and not (INTEGER.fullmatch(body) or
                    (name.startswith('__glibcxx_want_') and body == '')):
                if plumbing(body):
                    direct.add(body)
                else:
                    bad.add(name)
            direct.update(w['dependencies'])
            direct.update(inbound[w['file']])
        if name not in compiler and name not in guards and (name in PURE_CANDIDATES or plumbing(name)):
            dependency_edges[name] = direct | unknown
            dependency_seeds[name] = bad
    for name in compiler | guards:
        dependency_edges[name] = set()
        dependency_seeds[name] = set()
    # Values are ordinary reachability: a cycle contributes no new integers.
    propagate(value_sets, value_users)
    def values(name):
        return value_sets.get(name, ())

    dependencies = dependency_expander(dependency_edges, dependency_seeds)

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
