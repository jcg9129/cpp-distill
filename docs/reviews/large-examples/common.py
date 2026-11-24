# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only inputs and shared parsing conventions for the large-example review."""
import ast
import collections
import hashlib
import json
from pathlib import Path
import re
import sys

WORKTREE = Path(__file__).resolve().parents[3]
OUT = WORKTREE / 'measurements/large-examples'
ARTIFACTS = Path('/home/appuser/.cache/caide-gap/caide2/artifacts')
ATL = Path('/tmp/claude-1000/-workspace-cpp-distill/f0c845e5-23d4-411f-9b94-324515809aa1/scratchpad/atl')
NAMES = '''series-composition-quick polynomial-points-quick power-series-powers-and-roots
polynomial-gcd-quick polynomial-gcd-pade point-location-quick linear-recurrence-quick
polynomial-points-sampling link-cut-tree-basic polygon-overlay-quick power-series-quick
online-convolution-partitions online-convolution-quick'''.split()
sys.path.insert(0, str(OUT / 'python'))
from tree_sitter import Language, Parser
import tree_sitter_cpp
P = Parser(Language(tree_sitter_cpp.language()))

# Reuse the previous review's function definitions, without running its population
# selection or reading its summary.tsv. This intentionally fails if helpers move.
_previous = WORKTREE / 'docs/reviews/caide-gap-analysis/analyze.py'
_wanted = {'norm', 'name', 'declarations', 'd_report', 'c_report', 'ckind', 'group'}
_tree = ast.parse(_previous.read_text())
_nodes = [n for n in _tree.body if isinstance(n, ast.FunctionDef) and n.name in _wanted]
assert {n.name for n in _nodes} == _wanted
exec(compile(ast.Module(body=_nodes, type_ignores=[]), str(_previous), 'exec'))
_previous_group = group


def group(reason):
    if reason.startswith('D3e rule 7: root:'):
        return 'roots/effects'
    return _previous_group(reason)


def folder(example):
    matches = list(ARTIFACTS.glob('caide-compare-shard-*/examples/' + example))
    assert len(matches) == 1, (example, matches)
    return matches[0]


def example_path(example):
    matches = [p for p in (ATL / 'docs/examples').glob('*/*.cpp')
               if p.parent.name + '-' + p.stem == example]
    assert len(matches) == 1, (example, matches)
    return matches[0]


def source_file(report_file, example):
    return ATL / 'include' / report_file if report_file.startswith('atl/') else example_path(example)


def reports(path):
    raw, unique = d_report(path)
    for row in raw.values():
        if not row['file'].startswith('atl/') or row['file'].startswith('atl/docs/'):
            row['file'] = 'example'
    return raw, unique


def effective(raw, row):
    seen = set()
    while row['reason'] == 'family aggregation' and row['cause'] in raw:
        assert row['id'] not in seen
        seen.add(row['id'])
        row = raw[row['cause']]
    return row


def cause_chain(raw, row):
    chain, seen = [], set()
    while row['id'] not in seen:
        seen.add(row['id'])
        chain.append({k: row[k] for k in ('id', 'label', 'reason', 'file', 'source_line')})
        if row['cause'] not in raw:
            break
        row = raw[row['cause']]
    return chain


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def dump(name, value):
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / name).write_text(json.dumps(value, indent=2) + '\n')
