# SPDX-License-Identifier: GPL-3.0-or-later
"""Streaming evidence must retain the exact in-memory classifications/digests."""
import copy
import json
import random

from audit import apply_trust
from capture import write
from merge import baseline_text, canonical, delta_sets, digest, merge
from report_store import ReportStore, report_chunks, report_digest, write_evidence
from test_embed import report


def test_disk_roots_merge_and_evidence_match_memory(tmp_path):
    reports = [report(), report('other', reproduced=False)]
    reports[1]['observations'] = {'hosted': '0'}
    # Dotted keys are header spellings, not ijson path components.
    for r in reports:
        r['audit']['roots']['ext/pb_ds/detail.hpp'] = copy.deepcopy(r['audit']['roots']['charconv'])
    expected = merge(copy.deepcopy(reports), [r['build'] for r in reports])
    store = ReportStore(tmp_path / 'roots.sqlite')
    try:
        loaded = []
        for i, r in enumerate(reports):
            path = tmp_path / f'{i}-library.json'
            # Also exercise input whose fields are not sorted or pretty-printed.
            path.write_text(json.dumps(r))
            loaded.append(store.load(path, i))
            assert ''.join(report_chunks(loaded[-1])) == canonical(r)
        actual = merge(loaded, [r['build'] for r in loaded])
        assert baseline_text(actual) == baseline_text(expected)
        for r in loaded:
            apply_trust(r['audit'], {})
            expanded = json.loads(''.join(report_chunks(r)))
            assert report_digest(r) == digest(expanded)
        evidence = tmp_path / 'evidence.json'
        write_evidence(evidence, loaded, actual['generator'])
        assert json.loads(evidence.read_text())['builds'] == {
            r['build']: json.loads(''.join(report_chunks(r))) for r in loaded}
    finally:
        store.close()


def slow_delta(sets):
    result, previous = {}, []
    for key, entries in sorted(sets.items(), key=lambda kv: (len(kv[1]), canonical(kv[1]))):
        best = dict(entries=entries)
        cost = len(canonical(best))
        for base, old in previous:
            changes = {n: v for n, v in entries.items() if old.get(n) != v}
            removed = sorted(old.keys() - entries.keys())
            candidate = dict(base=base, entries=changes)
            if removed:
                candidate['removed'] = removed
            size = len(canonical(candidate))
            if size < cost:
                best, cost = candidate, size
        result[key] = best
        previous.append((key, entries))
    return result


def test_delta_size_arithmetic_matches_original_search():
    rng = random.Random(71)
    names = [f'name_{i}' for i in range(40)] + ['é', 'quote"', '\\', '𐐀']
    for _ in range(30):
        sets = {str(i): {n: rng.choice(['one', 'two', 'é', 'quote"']) for n in names if rng.randrange(4)}
                for i in range(15)}
        assert delta_sets(sets) == slow_delta(sets)


def test_shared_capture_records_preserve_revocable_matrix_trust(tmp_path):
    from audit import compact_audit, root_macros
    from probe import trusted_build_inputs
    a = report(condition='_GLIBCXX_HOSTED')
    a['observations'] = {'hosted': '1'}
    a['audit']['roots']['second.hpp'] = copy.deepcopy(a['audit']['roots']['charconv'])
    apply_trust(a['audit'], trusted_build_inputs(a, [a]))
    expanded = copy.deepcopy(a)
    compact_audit(a['audit'])
    assert len(a['audit']['root_records']) == 1
    assert a['audit']['roots']['charconv']['macros'] == a['audit']['roots']['second.hpp']['macros']
    b = report('other', condition='_GLIBCXX_HOSTED', reproduced=False)
    b['observations'] = {'hosted': '0'}
    expected = merge([expanded, copy.deepcopy(b)], [a['build'], b['build']])
    path = tmp_path / 'a-library.json'
    write(path, a)
    store = ReportStore(tmp_path / 'shared.sqlite')
    try:
        loaded = store.load(path, 0)
        actual = merge([loaded, b], [a['build'], b['build']])
        entry = root_macros(loaded['audit'], loaded['audit']['roots']['charconv'])['__cpp_lib_test']
        assert not entry['independent'] and '_GLIBCXX_HOSTED' in entry['unrecognised']
        assert report_digest(loaded) == digest(json.loads(''.join(report_chunks(loaded))))
        # Compact capture encoding changes evidence identity, but no facts.
        for key in actual['builds']:
            expected['builds'][key]['evidence_sha256'] = actual['builds'][key]['evidence_sha256']
        assert baseline_text(actual) == baseline_text(expected)
    finally:
        store.close()


def test_shared_records_recompact_and_reject_invalid_references():
    import pytest
    from audit import compact_audit, root_macros
    r = report()
    compact_audit(r['audit'])
    before = copy.deepcopy(r['audit'])
    compact_audit(r['audit'])
    assert r['audit'] == before
    for reference in (-1, len(r['audit']['root_records'])):
        r['audit']['roots']['charconv']['macros']['__cpp_lib_test'] = reference
        with pytest.raises(ValueError, match='invalid library audit record reference'):
            root_macros(r['audit'], r['audit']['roots']['charconv'])


def test_subset_cannot_be_used_for_reviewed_check(tmp_path):
    import subprocess
    import sys
    from audit import ROOT
    output = tmp_path / 'library.json'
    result = subprocess.run([sys.executable, str(ROOT / 'ci/library/merge.py'), str(tmp_path),
                             '--output', str(output), '--allow-subset', '--check', str(output)],
                            capture_output=True, text=True, timeout=30)
    assert result.returncode and '--allow-subset cannot be used with --check' in result.stderr
    assert not output.exists()
