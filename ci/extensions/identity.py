# SPDX-License-Identifier: GPL-3.0-or-later
"""A2 identity registry and textual definition reports (including inactive code)."""
import json
import re
from generate import ROOT, LIBRARIES, IDENTIFIER, directive_text


def registry(path=ROOT / 'data/names/identity-macros.txt'):
    result = {}
    previous = ''
    for line in path.read_text(encoding='utf-8').splitlines():
        if not line or line.startswith('#'):
            continue
        name, *libraries = line.split()
        if (not IDENTIFIER.fullmatch(name) or name <= previous or not libraries
                or len(set(libraries)) != len(libraries) or not set(libraries) <= set(LIBRARIES)):
            raise ValueError(f'{path}: invalid identity entry: {line}')
        result[name] = set(libraries)
        previous = name
    if not result:
        raise ValueError(f'{path}: empty identity registry')
    return result


def definitions(text):
    return set(re.findall(r'^[ \t]*#[ \t]*define[ \t]+([A-Za-z_][A-Za-z_0-9]*)',
                          directive_text(text, mask_literals=True), re.M))


def validate_report(report, policy):
    library = report.get('library')
    found = report.get('definitions')
    if report.get('version') != 1 or library not in LIBRARIES or not isinstance(found, dict):
        raise ValueError('invalid A2 identity report')
    violations = []
    for name, files in sorted(found.items()):
        if name not in policy or not isinstance(files, list) or not files or not all(isinstance(p, str) and p for p in files):
            raise ValueError(f'{library}: invalid identity definition entry {name}')
        if library in policy[name]:
            violations.append(f'{name}: {", ".join(files)}')
    if violations:
        raise ValueError(f'A2 identity violation for {library}: ' + '; '.join(violations))


def scan_identities(library, files, output, provenance, policy=None):
    policy = registry() if policy is None else policy
    found = {}
    for file in sorted(files):
        for name in sorted(definitions(file.read_bytes().decode('utf-8', errors='replace')) & policy.keys()):
            found.setdefault(name, []).append(file.as_posix())
    report = dict(version=1, library=library, provenance=provenance, definitions=found)
    output.write_text(json.dumps(report, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    # Write evidence even on failure. Known library exclusions are still reported.
    validate_report(report, policy)
    return len(found)
