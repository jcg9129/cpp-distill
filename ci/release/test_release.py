# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline release gates. All HTTP and executable calls here are mocked."""
import contextlib
import copy
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import urllib.error
import zipfile

import release
import publish


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        (release.ROOT / ".checks").mkdir(exist_ok=True)
        self.commit = "a" * 40
        self.identity = release.identity("tag", "v1.2.3", "push", False, self.commit)

    def temp(self):
        return tempfile.TemporaryDirectory(dir=release.ROOT / ".checks")

    def test_tag_and_branch_identity_and_ref_injection(self):
        self.assertEqual(self.identity["version"], "1.2.3")
        self.assertFalse(self.identity["dry_run"])
        preview = release.identity("branch", "feature/release", "workflow_dispatch", True, self.commit)
        self.assertEqual(preview["version"], "0.0.0-dev+aaaaaaaaaaaa")
        self.assertIsNone(preview["tag"])
        self.assertTrue(release.identity("tag", "v1.2.3-rc.1+build.2", "workflow_dispatch", True, self.commit)["dry_run"])
        for kind, name, event, dry in [("branch", "main", "workflow_dispatch", False),
                                       ("branch", "main", "push", False),
                                       ("tag", "v../bad", "push", False),
                                       ("tag", "v1.2.3\nmalicious", "push", False)]:
            with self.subTest(name=name, dry=dry), self.assertRaises(ValueError):
                release.identity(kind, name, event, dry, self.commit)

    def test_committed_license_text_and_notice_hashes(self):
        release.verify_licenses()
        text = (release.ROOT / "LLVM-LICENSE.txt").read_text()
        self.assertIn("LLVM Exceptions to the Apache 2.0 License", text)

    def sdk(self):
        measurements = dict(status="success", platform="linux-x86_64", llvm_version="23.1.5", sdk_rev=1,
                            source_archive={"sha256": "c" * 64},
                            cmake_settings={key: "OFF" for key in release.OPTIONAL})
        acquisition = dict(source="release", platform="linux-x86_64", sha256="b" * 64,
                           verification="release SHA256SUMS (including every split part)")
        return measurements, acquisition

    def evidence(self, measurements, acquisition, corrupt=False, source_digest="c" * 64):
        data = json.dumps(measurements).encode()
        sums = (hashlib.sha256(data).hexdigest() if not corrupt else "d" * 64) + "  measurements-linux-x86_64.json\n"
        sums += source_digest + "  " + release.LLVM_SOURCE + "\n"
        sums += "b" * 64 + "  llvm-sdk-23.1.5-1-linux-x86_64.tar.xz\n"
        with patch.object(release, "download", side_effect=[sums.encode(), data]):
            return release.sdk_evidence("owner/repo", "linux-x86_64", acquisition)

    def test_sdk_evidence_has_archive_and_matching_corresponding_source(self):
        measurements, acquisition = self.sdk()
        evidence = self.evidence(measurements, acquisition)
        self.assertEqual(evidence["archive_sha256"], "b" * 64)
        self.assertEqual(evidence["source_sha256"], "c" * 64)

    def test_sdk_fails_on_changed_settings_source_identity_or_measurements(self):
        measurements, acquisition = self.sdk()
        for setting in release.OPTIONAL:
            altered = copy.deepcopy(measurements)
            altered["cmake_settings"][setting] = "ON"
            with self.subTest(setting=setting), self.assertRaises(ValueError):
                self.evidence(altered, acquisition)
        for field, value in [("platform", "linux-arm64"), ("status", "failed"), ("llvm_version", "24.0.0")]:
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.evidence(dict(measurements, **{field: value}), acquisition)
        with self.assertRaises(ValueError):
            self.evidence(measurements, acquisition, corrupt=True)
        with self.assertRaises(ValueError):
            self.evidence(measurements, acquisition, source_digest="e" * 64)
        with self.assertRaises(ValueError):
            self.evidence(measurements, dict(acquisition, source="official"))
        with self.assertRaises(ValueError):
            self.evidence(measurements, dict(acquisition, sha256="e" * 64))

    def test_static_compression_xml_cannot_escape_dynamic_audit(self):
        release.check_link_commands("g++ -static-libstdc++ libLLVMSupport.a -lm -o distill")
        for command in ["g++ -lz", "g++ -lzstd", "g++ /sdk/lib/libxml2.a",
                        "cl C:\\sdk\\lib\\zlibstatic.lib", "clang++ /usr/lib/libzstd.a", "cl zlib.lib"]:
            with self.subTest(command=command), self.assertRaises(ValueError):
                release.check_link_commands(command)

    def fixture(self, root, sdk_platform):
        (root / "results").mkdir(exist_ok=True)
        (root / "isolated").mkdir(exist_ok=True)
        suffix = ".exe" if sdk_platform.startswith("windows-") else ""
        binary = root / "isolated" / ("distill" + suffix)
        binary.write_bytes(b"fake executable\n")
        binary.chmod(0o755)
        (root / "isolated" / ("distill_prune_worker" + suffix)).write_bytes(b"do not ship")
        for name in release.DOCUMENTS:
            shutil.copy2(release.ROOT / name, root / name)
        value = dict(self.identity, platform=sdk_platform, sdk={"source_sha256": "c" * 64})
        (root / "results/release.json").write_text(json.dumps(value))
        (root / "results/BUILDINFO.txt").write_text("compiler and flags provenance\n")
        return value

    def test_tar_and_zip_ship_only_one_binary_plus_documents_and_provenance(self):
        for sdk_platform in ("linux-x86_64", "windows-x86_64"):
            with self.subTest(platform=sdk_platform), self.temp() as directory:
                root = Path(directory)
                value = self.fixture(root, sdk_platform)
                result = subprocess.CompletedProcess([], 0, release.version_line(value), "")
                with patch.object(release.subprocess, "run", return_value=result) as run:
                    release.package(sdk_platform, root)
                    self.assertEqual(run.call_args.kwargs["timeout"], 120)
                archive = next((root / "release-assets").glob("*.zip" if sdk_platform.startswith("windows-") else "*.tar.gz"))
                if archive.suffix == ".zip":
                    with zipfile.ZipFile(archive) as stream:
                        members = stream.namelist()
                else:
                    with tarfile.open(archive) as stream:
                        members = [p.name for p in stream if p.isfile()]
                        binary = next(p for p in stream.getmembers() if p.name.endswith("/distill"))
                        if os.name != "nt":
                            self.assertTrue(binary.mode & 0o111)
                expected = set(release.DOCUMENTS) | {"BUILDINFO.txt", "distill.exe" if sdk_platform.startswith("windows-") else "distill"}
                self.assertEqual({Path(p).name for p in members}, expected)
                self.assertEqual(len(members), len(expected))

    def test_wrong_binary_version_cannot_be_packaged(self):
        with self.temp() as directory:
            root = Path(directory)
            self.fixture(root, "linux-x86_64")
            with patch.object(release.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "distill wrong\n", "")):
                with self.assertRaises(ValueError):
                    release.package("linux-x86_64", root)
            self.assertFalse((root / "release-assets").exists())

    def bundle(self, root):
        for sdk_platform in release.PLATFORMS:
            value = dict(self.identity, platform=sdk_platform, sdk={"source_sha256": "c" * 64})
            (root / f"{sdk_platform}.json").write_text(json.dumps(value))
            suffix = ".zip" if sdk_platform.startswith("windows-") else ".tar.gz"
            (root / f"distill-{value['version']}-{sdk_platform}{suffix}").write_bytes(sdk_platform.encode())

    def test_bundle_has_exact_checksums_and_two_source_links(self):
        with self.temp() as directory:
            root = Path(directory)
            self.bundle(root)
            release.collect(root, "owner/repo")
            sums = release.checksums((root / "SHA256SUMS").read_text())
            self.assertEqual(len(sums), 5)
            for name, digest in sums.items():
                self.assertEqual(digest, release.sha256(root / name))
            notes = (root / "RELEASE-NOTES.md").read_text()
            self.assertIn("https://github.com/owner/repo/archive/v1.2.3.tar.gz", notes)
            self.assertIn("/releases/download/llvm-23.1.5-1/llvm-project-23.1.5.src.tar.xz", notes)
            self.assertIn("GPL-3.0", notes)

    def test_incomplete_or_inconsistent_bundle_cannot_release(self):
        for field, replacement in [("version", "9.9.9"), ("commit", "d" * 40),
                                    ("sdk", {"source_sha256": "e" * 64})]:
            with self.subTest(field=field), self.temp() as directory:
                root = Path(directory)
                self.bundle(root)
                path = root / "linux-x86_64.json"
                value = json.loads(path.read_text())
                value[field] = replacement
                path.write_text(json.dumps(value))
                with self.assertRaises(ValueError):
                    release.collect(root, "owner/repo")
        with self.temp() as directory:
            root = Path(directory)
            self.bundle(root)
            next(root.glob("*.zip")).unlink()
            with self.assertRaises(ValueError):
                release.collect(root, "owner/repo")

    def test_publisher_refuses_dry_run_without_any_http_or_token_access(self):
        with self.temp() as directory:
            root = Path(directory)
            (root / "linux-x86_64.json").write_text(json.dumps(dict(self.identity, dry_run=True)))
            with patch.object(publish, "request") as request, patch.dict(os.environ, {}, clear=True):
                with self.assertRaises(ValueError):
                    publish.publish(root)
                request.assert_not_called()

    def test_http_error_body_is_reported_including_upload_errors(self):
        for method in ("GET", "POST", "PATCH"):
            error = urllib.error.HTTPError("https://api.github.com/test", 403, "Forbidden", {},
                                           io.BytesIO(b'{"message":"release denied"}'))
            output = io.StringIO()
            with self.subTest(method=method), patch.object(publish.urllib.request, "urlopen", side_effect=error):
                with contextlib.redirect_stdout(output), self.assertRaises(urllib.error.HTTPError):
                    publish.request(method, error.url, "explicit-test-token")
            self.assertIn('HTTP 403: {"message":"release denied"}', output.getvalue())
            self.assertNotIn("explicit-test-token", output.getvalue())
            error.close()

    def test_publisher_finalizes_only_after_six_successful_asset_uploads(self):
        for fail_upload in (False, True):
            with self.subTest(fail_upload=fail_upload), self.temp() as directory:
                root = Path(directory)
                self.bundle(root)
                release.collect(root, "owner/repo")
                operations = []

                def request(method, url, token, data=None, content_type="application/json"):
                    operations.append((method, url, data))
                    if method == "GET":
                        return {"object": {"type": "commit", "sha": self.commit}}
                    if url.endswith("/releases"):
                        self.assertTrue(data["draft"])
                        return {"url": "https://api.github.com/release", "upload_url": "https://uploads.github.com/test{?name}"}
                    if method == "POST" and fail_upload:
                        raise RuntimeError("simulated upload rejection")
                    return {}

                env = dict(GITHUB_REF="refs/tags/v1.2.3", GITHUB_REPOSITORY="owner/repo", GH_TOKEN="test-token")
                with patch.dict(os.environ, env), patch.object(publish, "request", side_effect=request):
                    if fail_upload:
                        with self.assertRaises(RuntimeError):
                            publish.publish(root)
                        self.assertFalse(any(method == "PATCH" for method, _, _ in operations))
                    else:
                        publish.publish(root)
                        self.assertEqual(operations[-1][0], "PATCH")
                        self.assertEqual(operations[-1][2], {"draft": False})
                        uploads = [url for method, url, _ in operations if method == "POST" and "?name=" in url]
                        self.assertEqual(len(uploads), 6)
                        self.assertTrue(uploads[-1].endswith("name=SHA256SUMS"))

    def test_publisher_checks_existing_annotated_tag_before_mutation(self):
        with self.temp() as directory:
            root = Path(directory)
            self.bundle(root)
            release.collect(root, "owner/repo")
            responses = [{"object": {"type": "tag", "url": "https://api.github.com/tag"}},
                         {"object": {"type": "commit", "sha": "d" * 40}}]
            env = dict(GITHUB_REF="refs/tags/v1.2.3", GITHUB_REPOSITORY="owner/repo", GH_TOKEN="test-token")
            with patch.dict(os.environ, env), patch.object(publish, "request", side_effect=responses) as request:
                with self.assertRaises(ValueError):
                    publish.publish(root)
                self.assertEqual([call.args[0] for call in request.call_args_list], ["GET", "GET"])


if __name__ == "__main__":
    unittest.main()
