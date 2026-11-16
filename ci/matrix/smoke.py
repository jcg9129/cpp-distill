# SPDX-License-Identifier: GPL-3.0-or-later
"""Moved real-CLI checks, with one bounded invocation and report per observation."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def run(argv, cwd, env=None):
    return subprocess.run(list(map(str, argv)), cwd=cwd, env=env, capture_output=True,
                          text=True, encoding="utf-8", errors="replace", timeout=120)


class Smoke:
    def __init__(self, args):
        self.args = args
        self.binary = args.binary.resolve()
        self.fixtures = args.fixtures.resolve()
        self.output = args.output.resolve() / args.label
        self.output.mkdir(parents=True, exist_ok=True)
        self.rows = []
        self.gcc = args.compiler
        version = run([self.gcc, "-dumpfullversion", "-dumpversion"], Path.cwd())
        require(version.returncode == 0, version.stderr)
        self.version = version.stdout.strip()
        triple = run([self.gcc, "-dumpmachine"], Path.cwd())
        require(triple.returncode == 0, triple.stderr)
        self.triple = triple.stdout.strip()
        self.work = None

    def invoke(self, name, fixture, dialect, options=(), env=None, compiler=True, compile_flags=()):
        case = self.work / name
        shutil.copytree(self.fixtures / fixture, case)
        # All platforms use these paths; Windows exercises wide argv end-to-end.
        source = case / "source space café"
        source.mkdir()
        for item in list(case.iterdir()):
            if item.is_file():
                shutil.move(item, source / item.name)
        output = case / "submit space café.cpp"
        report = self.output / f"{name} report café.txt"
        argv = [self.binary, "-v", "-o", output, "--report", report, *options]
        argv += ([self.gcc, f"-std={dialect}", *compile_flags] if compiler else [])
        argv += [source / "main.cpp"]
        try:
            result = run(argv, case, env)
        except subprocess.TimeoutExpired as error:
            # TimeoutExpired output is bytes even when the process uses text=True.
            captured = b"\n".join(value for value in (error.stdout, error.stderr) if value)
            (self.output / f"{name}.log").write_text(
                json.dumps(list(map(str, argv)), ensure_ascii=False) + "\nTIMEOUT after 120 seconds\n" +
                captured.decode("utf-8", errors="replace"), encoding="utf-8")
            raise
        (self.output / f"{name}.log").write_text(
            json.dumps(list(map(str, argv)), ensure_ascii=False) + "\n" + result.stdout + result.stderr,
            encoding="utf-8")
        text = report.read_text(encoding="utf-8") if report.exists() else ""
        match = re.search(r"distill: wrote (pruned|flat|raw) \((local|target)\)", text)
        return result, text, output, match.group(1) if match else "error"

    def analysis(self, text, artifact):
        require(artifact in ("pruned", "flat"), "expected pruned or documented flat")
        require("pruning analysis (does not certify an artifact)" in text,
                "missing pruning analysis (worker may not have run)")
        require(re.search(r'^parse configuration: .+', text, re.M), "missing parse argv")
        require(not re.search(r"Clang parse error|invalid Clang parse configuration|unsupported Clang target|"
                              r"error-recovery AST|worker failed|worker executable unavailable", text, re.I),
                "Clang parse/worker failure")
        reasons = sorted(set(re.findall(r"^pruning disabled: (.+)$", text, re.M)))
        if artifact == "flat":
            require(reasons, "flat result without an analysis refusal reason")
        else:
            require("pruned: passed" in text, "pruned check did not pass")
        return "; ".join(reasons) if reasons else "analysis and pruned checks passed"

    def observe(self, name, dialect, action):
        row = dict(platform=self.args.platform, gcc=f"{self.gcc} {self.version} ({self.triple})",
                   dialect=dialect, check=name, result="error", reason="", passed=False)
        self.rows.append(row)
        try:
            row["result"], row["reason"] = action()
            row["passed"] = True
            print(f"PASS {name}: {row['result']}: {row['reason']}", flush=True)
        except Exception as error:
            row["reason"] = str(error)[:2000]
            print(f"FAIL {name}: {error}", flush=True)
        finally:
            (self.output / "rows.json").write_text(json.dumps(self.rows, indent=2) + "\n", encoding="utf-8")

    def cli_case(self, name, fixture, dialect, options=(), expected=None, sample=False, strip=False):
        sample_file = self.work / "sample.in"
        sample_file.write_bytes(b"")
        if sample:
            options = [*options, "--verify-run", str(sample_file)]
        result, text, output, artifact = self.invoke(name, fixture, dialect, options)
        require(result.returncode == 0, result.stderr)
        require(output.is_file() and output.stat().st_size, "output missing or empty")
        require("raw: passed" in text and "flat: passed" in text, "raw/flat checks did not pass")
        if expected:
            require(artifact == expected, f"expected {expected}, got {artifact}\n{text}")
        reason = "--no-prune requested" if "--no-prune" in options else self.analysis(text, artifact)
        if sample:
            require("runtime compared 1 samples" in text, "sample comparison missing")
        if strip:
            content = output.read_text(encoding="utf-8")
            require("distill: keep" not in content and "SPDX-License-Identifier" not in content,
                    "comments survived stripping")
            require("preserve_me" in content, "keep annotation lost")
            if "--no-prune" not in options:
                require("unused" not in content, "unused declaration survived pruning")
        if fixture == "pruned_typical_library" and artifact == "pruned":
            content = output.read_text(encoding="utf-8")
            require("SparseTable" not in content and all(
                token in content for token in ("Mint", "LazySegTree", "Fenwick", "DSU")),
                "typical-library retention/removal regression")
        return artifact, reason

    def help(self):
        result = run([self.binary, "--help"], self.work)
        (self.output / "help.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        require(result.returncode == 0 and "Usage: distill" in result.stdout, "--help failed")
        return "help", "CLI help passed"

    def profile(self, dialect, no_prune):
        # Fixture profile with explicit command facts, no claimed reproduced
        # build. Mirrors tests/support/profile.cpp without fabricating a snapshot.
        profile_name = f"matrix-fixture-gcc{self.version.split('.')[0]}-{dialect.replace('gnu++', 'gnupp').replace('c++', 'cpp')}"
        profile = self.work / f"{profile_name}.toml"
        profile.write_text(
            "# SPDX-License-Identifier: GPL-3.0-or-later\n"
            f"name = '{profile_name}'\ndescription = 'CI command-fact fixture'\n"
            f"triple = '{self.triple}'\ncompiler = {{ family = 'gcc', version = '{self.version}' }}\n"
            f"std = '{dialect}'\nflags = ['-DDISTILL_FIXTURE_JUDGE=1']\n"
            "libraries = ['libstdc++']\ncheck = []\nupdated = '2026-11-16'\n"
            "sources = ['https://example.org/matrix-fixture']\n", encoding="utf-8")
        name = "profile-flat" if no_prune else "profile-prune"
        options = ["--judge", str(profile)] + (["--no-prune"] if no_prune else [])
        result, text, output, artifact = self.invoke(
            name, "profile_macro_replace", dialect, options, compile_flags=["-DDISTILL_FIXTURE_JUDGE=0"])
        require(result.returncode == 0 and artifact == ("flat" if no_prune else "pruned"), result.stderr)
        require("flat: passed" in text and "#if: resolved true" in text, "profile condition not resolved")
        require('option change: "-DDISTILL_FIXTURE_JUDGE=0"' in text, "local macro not replaced")
        require("profile: matrix-fixture" in text and "return 1" not in output.read_text(encoding="utf-8"),
                "profile command macro was not used")
        if not no_prune:
            self.analysis(text, artifact)
        return artifact, "fixture profile command fact resolved by moved condition worker"

    def apple(self):
        env = dict(os.environ, PATH="/usr/bin:/bin:" + os.environ.get("PATH", ""))
        result, text, output, artifact = self.invoke("apple-default", "no_profile", "gnu++20",
                                                    env=env, compiler=False)
        require(result.returncode == 1 and not output.exists(), "default Apple g++ was accepted")
        require("default g++ is not GCC; name a real GCC" in result.stderr, result.stderr)
        return "refused", "default Apple g++ correctly asks for real GCC"

    def cross(self, name, fixture, no_prune):
        require(self.triple == "x86_64-w64-mingw32", f"wrong cross triple: {self.triple}")
        sample = self.work / "sample.in"
        sample.write_bytes(b"")
        options = ["--judge", "codeforces", "--verify-run", str(sample)]
        if no_prune:
            options.append("--no-prune")
        result, text, output, artifact = self.invoke(name, fixture, "c++23", options)
        require(result.returncode == 0 and output.is_file(), result.stderr)
        adapter = ""
        # Test the real shipped profile first. GCC 13's Ubuntu cross package has
        # no libstdc++exp. Assert that exact failure, then check a named adapter.
        if artifact == "raw":
            require("cannot find -lstdc++exp" in text and "flat rejected:" in text,
                    "unexpected Codeforces fallback\n" + text)
            require("configuration check:" in text and "triple=x86_64-w64-mingw32" in text,
                    "shipped-profile failure did not use cross check compiler")
            self.cross_runtime(text)
            adapter = "Codeforces adapter omits unavailable -lstdc++exp; shipped profile raw fallback asserted"
            profile = self.work / "codeforces-gcc14-cpp23.toml"
            shipped = Path("data/judges/codeforces-gcc14-cpp23.toml").read_text(encoding="utf-8")
            require(shipped.count(', "-lstdc++exp"') == 1, "profile link spelling changed")
            profile.write_text(shipped.replace(', "-lstdc++exp"', ''), encoding="utf-8")
            shutil.copy2("data/judges/codeforces-gcc14-cpp23-predefined.txt", self.work)
            options[1] = str(profile)
            result, text, output, artifact = self.invoke(name + "-adapter", fixture, "c++23", options)
            require(result.returncode == 0 and output.is_file(), result.stderr)
        require(artifact == "flat" if no_prune else artifact in ("flat", "pruned"), text)
        require(re.search(r'flat: passed\n  configuration check:.*x86_64-w64-mingw32-g\+\+', text),
                "cross check compiler not selected\n" + text)
        require("triple=x86_64-w64-mingw32" in text, "wrong selected triple")
        self.cross_runtime(text)
        reason = "--no-prune requested" if no_prune else self.analysis(text, artifact)
        return artifact, "; ".join(filter(None, [adapter, reason, "compatible check selected; target runs skipped"]))

    def cross_runtime(self, text):
        require("runtime skipped: can't run target binaries here" in text, "cross runtime was not skipped")
        # No baseline/checked executable is invoked, even if runner binfmt could
        # execute PE files. Compiler invocations and CLI workers remain allowed.
        require(not re.search(r'^process .* argv: "[^"\n]*\.exe"(?:\s|$)', text, re.M),
                "attempted to run a target executable")

    def execute(self):
        with tempfile.TemporaryDirectory(prefix=".matrix-smoke-", dir=self.output) as work:
            self.work = Path(work)
            dialect = self.args.std
            if self.args.cross:
                for fixture in ("no_profile", "pruned_typical_library"):
                    for no_prune in (True, False):
                        name = f"codeforces-{fixture}-{'flat' if no_prune else 'prune'}"
                        self.observe(name, "c++23", lambda n=name, f=fixture, p=no_prune: self.cross(n, f, p))
            else:
                if not self.args.parse_only:
                    self.observe("help", "-", self.help)
                    for fixture in ("no_profile", "pruned_typical_library"):
                        for no_prune in (True, False):
                            name = f"{fixture}-{'flat' if no_prune else 'prune'}"
                            self.observe(name, dialect, lambda n=name, f=fixture, p=no_prune:
                                         self.cli_case(n, f, dialect, ["--no-prune"] if p else [],
                                                       "flat" if p else (None if self.args.minimum else "pruned"), sample=True))
                    for no_prune in (True, False):
                        name = "strip-flat" if no_prune else "strip-prune"
                        self.observe(name, dialect, lambda n=name, p=no_prune: self.cli_case(
                            n, "pruned_strip_keep", dialect,
                            ["--strip-comments"] + (["--no-prune"] if p else []),
                            "flat" if p else "pruned", sample=True, strip=True))
                        if not self.args.minimum:
                            name = "profile-flat" if no_prune else "profile-prune"
                            self.observe(name, dialect, lambda p=no_prune: self.profile(dialect, p))
                    if self.args.apple_default:
                        self.observe("apple-default", "default", self.apple)
                if not self.args.smoke_only:
                    for dialect in ("c++17", "gnu++17", "c++20", "gnu++20", "c++23", "gnu++23"):
                        self.observe(dialect, dialect, lambda d=dialect: self.cli_case(
                            d, "stl_preprocess", d))
        return 0 if all(row["passed"] for row in self.rows) else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("isolated/distill.exe" if os.name == "nt" else "isolated/distill"))
    parser.add_argument("--fixtures", type=Path, default=Path("tests/cases/pipeline"))
    parser.add_argument("--output", type=Path, default=Path("results"))
    parser.add_argument("--platform", required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--std", default="gnu++20")
    parser.add_argument("--parse-only", action="store_true")
    parser.add_argument("--smoke-only", action="store_true")
    parser.add_argument("--minimum", action="store_true")
    parser.add_argument("--apple-default", action="store_true")
    parser.add_argument("--cross", action="store_true")
    args = parser.parse_args()
    raise SystemExit(Smoke(args).execute())
