# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthetic CSES-like library facts through the production audit/merge/embed path."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from audit import VERSION, classify
from embed import generate
from member_inventory import inventory
from merge import baseline_text, merge

reports = []
for index in range(3):
    # The version gate, opt-in and reset mirror GCC 13's float.h.
    files = {'0/cfloat': '#include <detail/float.h>\n#if __cplusplus >= 201103L\n'
                         '#ifndef FLT_EVAL_METHOD\n#define FLT_EVAL_METHOD __FLT_EVAL_METHOD__\n#endif\n#endif',
             '0/detail/float.h': '#if (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L) || '
                         '(defined(__cplusplus) && __cplusplus >= 201103L)\n'
                         '#undef FLT_EVAL_METHOD\n#ifdef __STDC_WANT_IEC_60559_TYPES_EXT__\n'
                         '#define FLT_EVAL_METHOD __FLT_EVAL_METHOD_TS_18661_3__\n#else\n'
                         '#define FLT_EVAL_METHOD __FLT_EVAL_METHOD__\n#endif\n#endif',
             '0/cmath': '', '0/float.h': '#define FLT_EVAL_METHOD 2',
             '0/version': f'#define FLT_EVAL_METHOD {index * 2}'}
    predefines = {'__FLT_EVAL_METHOD__': '#define __FLT_EVAL_METHOD__ 0',
                  '__FLT_EVAL_METHOD_TS_18661_3__': '#define __FLT_EVAL_METHOD_TS_18661_3__ 0',
                  '__cplusplus': '#define __cplusplus 202100L'}
    headers = {file[2:]: dict(status='ok', macros={} if file == '0/cmath' else
                             {'FLT_EVAL_METHOD': '0' if file == '0/cfloat' else
                              '2' if file == '0/float.h' else str(index * 2)})
               for file in files if file != '0/detail/float.h'}
    native = index == 2
    volatile = ['__FLT_EVAL_METHOD__', '__FLT_EVAL_METHOD_TS_18661_3__'] if native else []
    flags = ['-O2', '-Wall', *(['-march=native'] if native else [])]
    report = dict(schema=2, generator=VERSION, build=f'extensions-cses-fixture-{index}', gcc='13.3.0',
                  major='13', std='c++23', flags=flags, triple='x86_64-linux-gnu',
                  compiler='synthetic', provenance=None, reproduced=False, observations={}, volatile=volatile,
                  headers=headers, audit=classify(files, {n: v for n, v in predefines.items() if n not in volatile},
                                                 volatile, roots=headers),
                  member_inventory=inventory(files))
    reports.append(report)
data = merge(reports, [r['build'] for r in reports])
Path(sys.argv[1]).write_text(generate(baseline_text(data).encode(), set(headers)), encoding='utf-8')
