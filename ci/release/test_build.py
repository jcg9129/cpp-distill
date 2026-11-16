# SPDX-License-Identifier: GPL-3.0-or-later
"""Protect existing matrix behavior and provenance before build-tree deletion."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import release

spec = importlib.util.spec_from_file_location("matrix_build", release.ROOT / "ci/matrix/build.py")
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)


class BuildTests(unittest.TestCase):
    def setUp(self):
        (release.ROOT / ".checks").mkdir(exist_ok=True)

    @contextlib.contextmanager
    def fixture(self):
        with tempfile.TemporaryDirectory(dir=release.ROOT / ".checks") as directory:
            root = Path(directory)
            work = root / "spike-work"
            output = root / "results"
            output.mkdir()
            with patch.multiple(build, ROOT=root, WORK=work, BUILD=work / "cli-build", RESULTS=output):
                yield root

    def test_default_matrix_targets_and_toolchains_are_unchanged(self):
        for system, sdk_platform, compiler in [("Linux", "linux-arm64", "g++"),
                                                ("Darwin", "macos-arm64", "clang++"),
                                                ("Windows", "windows-x86_64", "cl")]:
            with self.subTest(system=system), self.fixture():
                result = subprocess.CompletedProcess([], 0)
                with patch.object(build.platform, "system", return_value=system), \
                        patch.object(build.subprocess, "run", return_value=result) as run, \
                        contextlib.redirect_stdout(io.StringIO()):
                    build.build(sdk_platform)
                configure = run.call_args_list[1].args[0]
                compile_command = run.call_args_list[2].args[0]
                self.assertIn("-DCMAKE_CXX_COMPILER=" + compiler, configure)
                self.assertFalse(any(arg.startswith("-DDISTILL_VERSION=") for arg in configure))
                self.assertEqual(compile_command[compile_command.index("--target") + 1:],
                                 ["distill", "distill_prune_worker", "--parallel", "2", "--verbose"])
                if system == "Darwin":
                    self.assertIn("-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0", configure)

    def test_release_injects_identity_and_records_before_isolation(self):
        with self.fixture() as root:
            value = release.identity("tag", "v1.2.3", "push", False, "a" * 40)
            (root / "results/release.json").write_text(json.dumps(value))
            with patch.object(build.subprocess, "run", return_value=subprocess.CompletedProcess([], 0)) as run, \
                    patch.object(release, "record") as record, patch.object(sys, "path", sys.path.copy()), \
                    contextlib.redirect_stdout(io.StringIO()):
                build.build("linux-x86_64", True)
                record.assert_called_once_with(build.BUILD, "linux-x86_64")
            configure = run.call_args_list[1].args[0]
            self.assertIn("-DDISTILL_VERSION=1.2.3", configure)
            self.assertIn("-DDISTILL_COMMIT=" + "a" * 40, configure)
            self.assertIn("-DDISTILL_LLVM_REVISION=llvmorg-23.1.5", configure)
            self.assertNotIn("distill_prune_worker", run.call_args_list[2].args[0])

    def test_isolation_removes_sdk_and_build_and_keeps_expected_binaries(self):
        for release_build in (False, True):
            with self.subTest(release_build=release_build), self.fixture() as root:
                build.BUILD.mkdir(parents=True)
                suffix = ".exe" if os.name == "nt" else ""
                for name in ("distill", "distill_prune_worker"):
                    (build.BUILD / (name + suffix)).write_bytes(b"fake binary")
                (build.WORK / "acquisition.json").write_text("{}")
                (build.WORK / "sdk").mkdir()
                (root / "tests/cases/pipeline").mkdir(parents=True)
                (root / "ci/matrix").mkdir(parents=True)
                (root / "ci/matrix/smoke.py").write_text("# fixture\n")
                build.isolate(release_build)
                self.assertFalse(build.WORK.exists())
                self.assertTrue((root / "isolated" / ("distill" + suffix)).is_file())
                self.assertEqual((root / "isolated" / ("distill_prune_worker" + suffix)).exists(), not release_build)
                evidence = json.loads((root / "results/isolation.json").read_text())
                self.assertTrue(evidence["sdk_deleted"])
                self.assertTrue(evidence["build_deleted"])

    def test_provenance_retains_toolchain_and_actual_commands(self):
        with self.fixture() as root:
            value = release.identity("tag", "v1.2.3", "push", False, "a" * 40)
            (root / "results/release.json").write_text(json.dumps(value))
            build.BUILD.mkdir(parents=True)
            (build.WORK / "acquisition.json").write_text("{}")
            (build.BUILD / "distill-toolchain.txt").write_text("Compiler: GCC 15.2.0\nFlags: -O3 -DNDEBUG\n")
            commands = ('g++ -O3 -c main.cpp -o CMakeFiles/distill.dir/src/cli/main.cpp.o\n'
                        'g++ CMakeFiles/distill.dir/src/cli/main.cpp.o -static-libstdc++ -o distill\n')
            sdk = dict(archive_sha256="b" * 64, source_sha256="c" * 64)
            previous = Path.cwd()
            try:
                os.chdir(root)
                with patch.dict(os.environ, {"GITHUB_REPOSITORY": "owner/repo"}), \
                        patch.object(release, "sdk_evidence", return_value=sdk), \
                        patch.object(release.subprocess, "check_output", return_value=commands):
                    release.record(build.BUILD, "linux-x86_64")
            finally:
                os.chdir(previous)
            info = (root / "results/BUILDINFO.txt").read_text()
            for required in ("distill version: 1.2.3", "LLVM revision: llvmorg-23.1.5", "b" * 64,
                             "GCC 15.2.0", "-O3 -DNDEBUG", "-static-libstdc++", "Build date (UTC):"):
                self.assertIn(required, info)
            self.assertEqual((root / "results/link-commands.txt").read_text(), commands)


if __name__ == "__main__":
    unittest.main()
