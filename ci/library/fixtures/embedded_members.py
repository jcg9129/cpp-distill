# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the test binary's inventory through the production merge/embed path."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from audit import VERSION, classify
from embed import generate
from member_inventory import inventory
from merge import baseline_text, merge

files = {'0/charconv': '#define __cpp_lib_fixture 1L\n#if 0\nvoid scanned_only_member();\n'
                      'void caf\\u00e9(); void \\U00010400();\n#endif'}
report = dict(schema=2, generator=VERSION, build='extensions-fixture', gcc='14.2.0',
              major='14', std='c++23', flags=[], triple='synthetic', compiler='synthetic',
              provenance=None, reproduced=False, observations={}, volatile=[],
              headers={'charconv': dict(status='ok', macros={'__cpp_lib_fixture': '1L'})},
              audit=classify(files, (), roots=['charconv']), member_inventory=inventory(files))
data = merge([report], [report['build']])
Path(sys.argv[1]).write_text(generate(baseline_text(data).encode(), {'charconv'}), encoding='utf-8')
