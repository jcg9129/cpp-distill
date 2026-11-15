# SPDX-License-Identifier: GPL-3.0-or-later
"""Cheap fixture checks: no SDK, compiler, Docker, network or runner mutation."""
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch
from common import extract

spec = importlib.util.spec_from_file_location("binary_audit", Path(__file__).with_name("binary-audit.py"))
binary_audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(binary_audit)


class DependencyAuditTests(unittest.TestCase):
    def audit(self, system, outputs):
        with patch.object(binary_audit.platform, "system", return_value=system), patch.object(binary_audit, "capture", side_effect=outputs):
            return binary_audit.audit(Path("fixture-binary"))

    def test_linux_numeric_glibc_order(self):
        result = self.audit("Linux", ["(NEEDED) Shared library: [libc.so.6]", "GLIBC_2.9 GLIBC_2.28 GLIBC_2.17"])
        self.assertTrue(result["pass"])
        self.assertEqual(result["highest_glibc"], "2.28")

    def test_linux_rejects_new_glibc_and_dynamic_cpp_runtime(self):
        result = self.audit("Linux", ["(NEEDED) Shared library: [libstdc++.so.6]", "GLIBC_2.34"])
        self.assertFalse(result["pass"])
        self.assertEqual(len(result["violations"]), 2)

    def test_linux_requires_symbol_evidence(self):
        self.assertFalse(self.audit("Linux", ["", ""])["pass"])

    def test_macos_deployment_and_dependencies(self):
        libraries = "stub:\n\t/usr/lib/libc++.1.dylib (compatibility version 1.0.0)\n\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0)\n"
        self.assertTrue(self.audit("Darwin", [libraries, "cmd LC_BUILD_VERSION\nminos 14.0\n"])["pass"])
        self.assertFalse(self.audit("Darwin", [libraries, "minos 13.0\n"])["pass"])
        self.assertFalse(self.audit("Darwin", [libraries + "\t/opt/homebrew/lib/libzstd.dylib (compatibility version 1.0.0)\n", "minos 14.0\n"])["pass"])

    def test_windows_static_crt_gate(self):
        self.assertTrue(self.audit("Windows", ["    KERNEL32.dll\n    PSAPI.DLL\n"])["pass"])
        for library in ["MSVCP140.dll", "VCRUNTIME140.dll", "ucrtbase.dll", "api-ms-win-crt-runtime-l1-1-0.dll"]:
            with self.subTest(library=library):
                self.assertFalse(self.audit("Windows", ["    KERNEL32.dll\n    " + library + "\n"])["pass"])


class MatrixTests(unittest.TestCase):
    def matrix(self, *args, success=True):
        result = subprocess.run([sys.executable, "ci/llvm/matrix.py", *args], text=True, capture_output=True, timeout=10)
        self.assertEqual(result.returncode == 0, success, result.stderr)
        return json.loads(result.stdout)["include"] if success else None

    def test_all_and_single_source_artifact(self):
        rows = self.matrix()
        self.assertEqual(len(rows), 5)
        self.assertEqual(sum(p["source_artifact"] for p in rows), 1)

    def test_official_excludes_intel_macos(self):
        rows = self.matrix("--official")
        self.assertEqual(len(rows), 4)
        self.assertNotIn("macos-x86_64", [p["platform"] for p in rows])

    def test_subset_reassigns_source_artifact(self):
        rows = self.matrix("--platforms", "windows-x86_64,macos-arm64")
        self.assertEqual(len(rows), 2)
        self.assertTrue(rows[0]["source_artifact"])

    def test_invalid_platform_or_empty_official_selection(self):
        self.matrix("--platforms", "typo", success=False)
        self.matrix("--platforms", "macos-x86_64", "--official", success=False)


class ExtractionTests(unittest.TestCase):
    def test_safe_tar_and_traversal_rejection(self):
        checks = Path(".checks")
        checks.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=checks) as directory:
            root = Path(directory)
            for filename, safe in [("sdk/include/test.h", True), ("../escape.h", False)]:
                archive = root / "fixture.tar.xz"
                with tarfile.open(archive, "w:xz") as stream:
                    member = tarfile.TarInfo(filename)
                    member.size = 3
                    stream.addfile(member, io.BytesIO(b"abc"))
                if safe:
                    extract(archive, root / "extracted")
                    self.assertEqual((root / "extracted" / filename).read_bytes(), b"abc")
                else:
                    with self.assertRaises(tarfile.FilterError):
                        extract(archive, root / "extracted")
                    self.assertFalse((root / "escape.h").exists())


if __name__ == "__main__":
    unittest.main()
