# SPDX-License-Identifier: GPL-3.0-or-later
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from identity import definitions, registry, scan_identities, validate_report
from merge import merge


class IdentityTests(unittest.TestCase):
    def test_textual_definitions(self):
        text = ('#if 0\n# de\\\nfine _MSC_\\\nVER 1\n#endif\n'
                '#/**/define/**/__clang__(x) x\n#undef _WIN32\n'
                '/*\n#define __APPLE__ 1\n*/\n// hide \\\n#define __MACH__ 1\n'
                'auto s = R"tag(\n#define __linux__ 1\n)tag";\n'
                '#define STR "/*"\n#define _WIN64 1\n')
        self.assertEqual(definitions(text), {'_MSC_VER', '__clang__', 'STR', '_WIN64'})

    def test_registry_scopes_and_validation(self):
        policy = registry()
        self.assertEqual(len(policy), 26)
        self.assertIn('mingw-w64', policy['_MSC_VER'])
        for name in ['_M_X64', '_M_AMD64', '_M_IX86', '_M_ARM', '_M_ARM64', '_M_ARM64EC', '_WIN64']:
            self.assertNotIn('mingw-w64', policy[name])
            self.assertIn('glibc', policy[name])
        with tempfile.TemporaryDirectory(dir='.') as temp:
            file = Path(temp) / 'policy'
            for text in ['_MSC_VER unknown\n', '_MSC_VER glibc glibc\n', '_MSC_VER glibc\n_MSC_VER glibc\n']:
                file.write_text(text)
                with self.assertRaises(ValueError):
                    registry(file)

    def test_scan_reports_inactive_violation_and_excluded_names(self):
        with tempfile.TemporaryDirectory(dir='.') as temp:
            root = Path(temp)
            header = root / 'header.h'; output = root / 'report.json'
            header.write_text('#if 0\n#define _M_X64 100\n#endif\n#undef _MSC_VER\n')
            self.assertEqual(scan_identities('mingw-w64', {header}, output, ['fixture']), 1)
            report = json.loads(output.read_text())
            self.assertEqual(report['definitions'], {'_M_X64': [header.as_posix()]})
            with self.assertRaisesRegex(ValueError, 'A2 identity violation for glibc: _M_X64'):
                scan_identities('glibc', {header}, output, ['fixture'])
            self.assertEqual(json.loads(output.read_text())['library'], 'glibc')
            header.write_text('#define _MSC_VER 1\n')
            with self.assertRaisesRegex(ValueError, '_MSC_VER'):
                scan_identities('mingw-w64', {header}, output, [])

    def test_merge_enforces_reports(self):
        with tempfile.TemporaryDirectory(dir='.') as temp:
            root = Path(temp)
            artifacts = root / 'artifacts'; artifacts.mkdir()
            (artifacts / 'glibc.txt').write_text('# generator ci/extensions/generate.py v2;\n')
            report = artifacts / 'glibc-identity.json'
            with self.assertRaises(FileNotFoundError):
                merge(artifacts, root / 'out')
            report.write_text(json.dumps(dict(version=1, library='glibc', definitions={'_MSC_VER': ['header.h']})))
            with self.assertRaisesRegex(ValueError, 'A2 identity violation'):
                merge(artifacts, root / 'out')
            report.write_text(json.dumps(dict(version=1, library='mingw-w64', definitions={})))
            with self.assertRaisesRegex(ValueError, 'mismatched'):
                merge(artifacts, root / 'out')
            report.write_text(json.dumps(dict(version=1, library='glibc', definitions={})))
            merge(artifacts, root / 'out')

    def test_malformed_report(self):
        for report in [dict(version=0, library='glibc', definitions={}),
                       dict(version=1, library='glibc', definitions={'_MSC_VER': []}),
                       dict(version=1, library='glibc', definitions={'NOT_REGISTERED': ['x']})]:
            with self.assertRaises(ValueError):
                validate_report(report, registry())


if __name__ == '__main__':
    unittest.main()
