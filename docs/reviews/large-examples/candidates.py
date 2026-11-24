# SPDX-License-Identifier: GPL-3.0-or-later
"""Audited source pools and local prototypes of proposed ATL header splits.

These edits apply ONLY to worktree-local copies of compact artifacts. They model
the narrow interfaces discussed in the review, not a portable distill rule or a
complete backwards-compatible ATL implementation. --verify compiles and compares
each changed prototype with the original example's stdout from profile.py.
"""
from common import *
import argparse
import concurrent.futures
import os
import subprocess

LABELS = {
    'S': 'optional mod_int streaming',
    'B': 'primality separate from sieve/static_vector',
    'W': '32-bit series product separate from wide limbs',
    'M': 'exact_field separate from matrix/elimination',
    'H': 'scalar hash header without other key specializations',
    'G': 'integral predicates separate from adaptive floating kernels',
    'T': 'composition default strategy separate from Brent-Kung',
    'F': 'direct NTT product with schoolbook fallback, optional FFT dispatcher',
    'L': 'link-cut vertex storage separate from edge registry',
}


def nodes(node):
    yield node
    for child in node.named_children:
        yield from nodes(child)


def view(buf):
    return P.parse(re.sub(rb'if (?:!)?consteval', lambda m: (b'if (true)' + b' ' * len(m[0]))[:len(m[0])], buf)).root_node


def prototype(x, keys):
    buf = (folder(x['example']) / 'distill-compact.cpp').read_bytes()
    edits, details = [], []
    ds = x['declarations']
    def erase(decl, why):
        edits.append((decl['start'], decl['end'], b''))
        details.append(dict(name=decl['name'], file=decl.get('report', {}).get('file'),
                            source_line=decl.get('report', {}).get('source_line'), why=why,
                            start=decl['start'], end=decl['end'], chain=decl.get('chain', [])))
    def headers(names, why):
        pos = 0
        for line, origin in zip(buf.splitlines(keepends=True), x['line_map']):
            if origin[0] in names:
                edits.append((pos, pos + len(line), b''))
            pos += len(line)
        details.append(dict(headers=sorted(names), why=why))
    for key in keys:
        if key == 'S':
            for d in ds:
                if d['report']['file'] == 'atl/nt/detail/uint128_stream.hpp':
                    erase(d, key)
            for d in x['unreported']:
                if d['name'] in ['atl::operator<<', 'atl::operator>>']:
                    erase(d, key)
        if key == 'B' and 'atl/ds/static_vector.hpp' in x['headers']:
            headers({'atl/ds/static_vector.hpp', 'atl/nt/sieve.hpp', 'atl/ds/detail/forward_element.hpp'}, key)
        if key == 'W':
            for d in ds:
                if d['name'] == 'atl::convolution_uint64':
                    erase(d, key)
                if d['name'] == 'atl::detail::series_product':
                    for n in nodes(view(buf[d['start']:d['end']])):
                        if n.type == 'if_statement' and n.text.startswith(b'if constexpr (Coefficient::mod() <='):
                            alt = n.child_by_field_name('alternative')
                            assert alt is not None
                            edits.append((d['start'] + alt.start_byte, d['start'] + alt.end_byte, b''))
                            details.append(dict(name=d['name'], why=key, bytes=alt.end_byte-alt.start_byte,
                                                chain=d['chain'], file=d['report']['file'], source_line=d['report']['source_line']))
        if key == 'M':
            headers({'atl/nt/matrix.hpp'}, key)
            for d in ds:
                if d['report']['file'] == 'atl/nt/gaussian_elimination.hpp' and d['name'] != 'atl::exact_field':
                    erase(d, key)
        if key == 'H':
            keep = {'atl::detail::splitmix64', 'atl::detail::initial_hash_seed',
                    'atl::detail::hash_seed_value', 'atl::detail::hash_seed',
                    'atl::detail::hashable_integer', 'atl::identity_hash', 'atl::identity_hash::operator()'}
            for d in ds:
                if d['report']['file'] != 'atl/utils/hash.hpp':
                    continue
                if d['name'] in keep or (d['name'] in {'atl::hash', 'atl::hash::operator()'} and d['report']['source_line'] in {191,198,206}):
                    continue
                erase(d, key)
        if key == 'G':
            for d in ds:
                if d['report']['file'] == 'atl/geometry/predicates.hpp' and d['report']['source_line'] < 318 and d['name'] != 'atl::geometry::detail::is_finite':
                    erase(d, key)
                if d['name'] in {'atl::geometry::exact::orientation', 'atl::geometry::exact::cross_sign'}:
                    for n in nodes(view(buf[d['start']:d['end']])):
                        if n.type == 'if_statement' and n.text.startswith(b'if constexpr (std::floating_point<T>)'):
                            alt = n.child_by_field_name('alternative')
                            replacement = buf[d['start'] + alt.named_children[0].start_byte:d['start'] + alt.named_children[0].end_byte]
                            edits.append((d['start'] + n.start_byte, d['start'] + n.end_byte, replacement))
        if key == 'T':
            for d in ds:
                if d['name'] in {'atl::detail::composition_brent_kung','atl::detail::composition_block_size'}:
                    erase(d, key)
                if d['name'] == 'atl::detail::composition_dispatch':
                    for n in nodes(view(buf[d['start']:d['end']])):
                        if n.type == 'if_statement' and n.text.startswith(b'if constexpr (Algorithm == composition_algorithm::schoolbook)'):
                            tail = n
                            while True:
                                alt = tail.child_by_field_name('alternative')
                                tail = alt.named_children[0]
                                if tail.type != 'if_statement':
                                    break
                            replacement = buf[d['start']+tail.start_byte:d['start']+tail.end_byte]
                            edits.append((d['start']+n.start_byte,d['start']+n.end_byte,replacement))
        if key == 'F':
            headers({'atl/nt/fft.hpp'}, key)
            for d in ds:
                if d['name'] in {'atl::detail::fft_split_convolution','atl::detail::garner_fits'}:
                    erase(d, key)
                if d['name'] == 'atl::convolution':
                    for n in nodes(view(buf[d['start']:d['end']])):
                        if n.type == 'if_statement' and n.text.startswith(b'if (true)') and b'fft_split_convolution' in n.text:
                            edits.append((d['start']+n.start_byte,d['start']+n.end_byte,b'return naive();'))
        if key == 'L' and x['example'] == 'link-cut-tree-basic':
            headers({'atl/ds/flat_hash_map.hpp','atl/ds/detail/hash_table.hpp',
                     'atl/ds/detail/heterogeneous_key.hpp','atl/ds/detail/key_value_entry.hpp'},key)
            for d in ds:
                if d['name'] == 'atl::detail::forest_edge_hash':
                    erase(d,key)
                if d['name'] == 'atl::detail::forest_edge_registry':
                    edits.append((d['start'],d['end'],b'struct forest_edge_registry;'))
    # Union deletion intervals; a record and its extracted members may overlap.
    deleted = bytearray(len(buf))
    replacements = []
    for lo, hi, replacement in edits:
        if replacement:
            replacements.append((lo,hi,replacement))
        else:
            deleted[lo:hi] = b'\1'*(hi-lo)
    replacements.sort()
    assert all(a[1] <= b[0] for a,b in zip(replacements,replacements[1:]))
    for lo, hi, _ in replacements:
        assert not any(deleted[lo:hi]), (x['example'],keys,lo,hi)
    starts = {a:(b,c) for a,b,c in replacements}
    out, pos = bytearray(), 0
    while pos < len(buf):
        if pos in starts:
            hi, replacement = starts[pos]
            out.extend(replacement)
            pos = hi
        else:
            if not deleted[pos]:
                out.append(buf[pos])
            pos += 1
    return bytes(out), len(buf)-len(out), details


def verify_one(item):
    example, scenario, code = item
    dest = OUT / 'prototypes' / scenario / example
    dest.mkdir(parents=True, exist_ok=True)
    source = dest / 'example.cpp'
    source.write_bytes(code)
    args = ['g++','-std=c++23','-O0',str(source),'-o',str(dest/'example')]
    result = subprocess.run(args, capture_output=True, timeout=180,
                            env=dict(os.environ,TMPDIR=str(OUT/'tmp')))
    (dest/'compile.log').write_bytes(result.stdout+result.stderr)
    assert result.returncode == 0, (scenario,example,result.stderr.decode()[-3000:])
    run = subprocess.run([str(dest/'example')],cwd=dest,capture_output=True,timeout=30)
    assert run.returncode == 0, (scenario,example,run.stderr)
    assert run.stdout == (OUT/'runs'/example/'stdout.txt').read_bytes(), (scenario,example,run.stdout)
    return dict(example=example,scenario=scenario,bytes=len(code),stdout_matches=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify',action='store_true')
    parser.add_argument('--only',nargs='+',help='Verify only these scenarios; measurements still include all.')
    args = parser.parse_args()
    data = json.loads((OUT/'analysis.json').read_text())
    scenarios = list(LABELS) + ['SBW','HG','LH','SBWMHGT','SBWMHGTF']
    rows, jobs = [], []
    for scenario in scenarios:
        for x in data:
            code, saved, details = prototype(x, scenario)
            projected = (len(code)*8736+9999)//10000
            rows.append(dict(example=x['example'],scenario=scenario,saved=saved,
                             compact_after=len(code),projected=projected,under=projected<65536,
                             details=details))
            if saved and (not args.only or scenario in args.only):
                jobs.append((x['example'],scenario,code))
        selected = rows[-len(data):]
        print(scenario, 'saving',sum(r['saved'] for r in selected),'under',sum(r['under'] for r in selected),
              'new',[r['example'] for r,x in zip(selected,data) if r['under'] and x['projected']>=65536],flush=True)
    dump('candidates.json',rows)
    if args.verify:
        with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
            verified = []
            for row in pool.map(verify_one,jobs):
                verified.append(row)
                print('PASS',row['scenario'],row['example'],flush=True)
        previous = json.loads((OUT/'prototype-checks.json').read_text()) if (OUT/'prototype-checks.json').exists() else []
        merged = {(r['scenario'],r['example']):r for r in previous + verified}
        dump('prototype-checks.json',list(merged.values()))


if __name__ == '__main__':
    main()
