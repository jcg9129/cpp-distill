// SPDX-License-Identifier: GPL-3.0-or-later
#include "profile.h"
#include "core/path.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace distill::testing {
namespace {
template<class T> T take(Result<T> r) { if (!r.ok()) throw r.error(); return std::move(r.value()); }
void definition(std::ostream& out, const std::string& name, const Macro& m) {
  out << "#define " << name;
  if (m.functionLike) {
    out << '(';
    for (std::size_t i = 0; i < m.parameters.size(); ++i) { if (i) out << ','; out << m.parameters[i]; }
    out << ')';
  }
  out << ' ' << m.replacement << '\n';
}
}
Result<Profile> captureProfile(const std::filesystem::path& compiler, const std::filesystem::path& directory) {
  try {
    auto cmd = take(parseCompileCommand({pathToUtf8(compiler), "main.cpp", "-std=gnu++20", "-O0", "-fsigned-char", "-DDISTILL_FIXTURE_JUDGE=1"}, directory));
    ProbeOptions po; po.temporaryRoot = directory;
    auto c = take(probeConfiguration(cmd, "local", po));
    const auto& p = c.probe;
    const auto version = std::to_string(p.identity.major) + '.' + std::to_string(p.identity.minor) + '.' + std::to_string(p.identity.patch);
    const auto builtins = specialBuiltins(p.identity);
    const bool complete = !builtins.empty();
    ProcessSpec versionSpec; versionSpec.cwd = directory;
    versionSpec.argv = {pathToUtf8(compiler), "--version"};
    const auto versionRun = take(runProcess(versionSpec));
    if (versionRun.exitCode || versionRun.timedOut || versionRun.stdoutTruncated || versionRun.stderrTruncated) throw Error{"fixture version probe failed"};
    std::string versionOutput;
    for (char ch : versionRun.stdoutText) versionOutput += ch == '\n' ? "\\n" : std::string(1, ch);
    const auto file = directory / "fixture.toml";
    {
      std::ofstream out(file, std::ios::binary);
      out << "# SPDX-License-Identifier: GPL-3.0-or-later\nname = 'fixture'\ndescription = 'runtime local compiler fixture'\ntriple = " << std::quoted(p.triple)
          << "\ncompiler = { family = 'gcc', version = '" << version << "' }\nstd = 'gnu++20'\nflags = ['-O0', '-fsigned-char', '-DDISTILL_FIXTURE_JUDGE=1']\n"
          << "build = { package = 'test:runtime=" << version << "' }\nlibraries = ['" << (p.triple.find("mingw") != p.triple.npos ? "mingw-w64" : p.triple.find("darwin") != p.triple.npos ? "macos-libc" : "glibc")
          << "', 'libstdc++']\npredefined = 'fixture-predefined.txt'\ncheck = ['distill-missing-check-719239']\nupdated = '2026-11-16'\nsources = ['https://example.org/test-fixture']\n";
      if (!out) throw Error{"cannot write fixture profile"};
    }
    {
      std::ofstream out(directory / "fixture-predefined.txt", std::ios::binary);
      out << "# SPDX-License-Identifier: GPL-3.0-or-later\n# distill-snapshot: 1\n# compiler-path: " << pathToUtf8(compiler)
          << "\n# compiler-version: " << versionOutput << "\n# gcc-version: " << version << "\n# dumpmachine: " << p.triple << "\n# profile-triple: " << p.triple
          << "\n# build: package=test:runtime=" << version << "\n# reproduced: " << (complete ? "yes" : "no") << "\n# std: gnu++20\n# builtin-registry: "
          << (complete ? "gcc-" + std::to_string(p.identity.major) + '.' + std::to_string(p.identity.minor) : "none")
          << "\n# native-model: none\n# flag: -O0\n# flag: -fsigned-char\n# flag: -DDISTILL_FIXTURE_JUDGE=1\n# end-header\n";
      for (const auto& [name, m] : p.macros) {
        if (p.startupMacros.contains(name)) out << "# startup-header stdc-predef.h ";
        else if (name == "DISTILL_FIXTURE_JUDGE") out << "# command-operation ";
        definition(out, name, m);
      }
      for (const auto& name : builtins) {
        const auto defined = p.special.at(name);
        if (!p.macros.contains(name)) out << "# state " << name << (defined ? " defined-unknown" : " undefined") << " special-builtin\n";
        out << "# probe " << name << (defined ? " defined\n" : " undefined\n");
      }
      if (!out) throw Error{"cannot write fixture snapshot"};
    }
    return loadProfile(file);
  } catch (const Error& e) { return e; }
  catch (const std::exception& e) { return Error{e.what()}; }
}
} // namespace distill::testing
