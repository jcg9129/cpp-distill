// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/pipeline/pipeline.h"
#include "core/path.h"
#include "evaluate.h"
#include "profile.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using namespace distill;
template<class T> T take(Result<T> r) { if (!r.ok()) throw std::runtime_error(r.error().message); return std::move(r.value()); }
void check(bool b, const std::string& s) { if (!b) throw std::runtime_error(s); }
std::string read(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary); check(bool(f), "cannot read " + pathToUtf8(p));
  return {std::istreambuf_iterator<char>(f), {}};
}
void write(const std::filesystem::path& p, const std::string& s) {
  std::ofstream f(p, std::ios::binary); f << s; check(bool(f), "cannot write test file");
}
std::string uncomment(const std::string& text) {
  std::istringstream in(text); std::string line, result;
  while (std::getline(in, line)) if (!line.starts_with('#')) result += line + '\n';
  return result;
}
// Same shell-style quoting as the inline/resolver case runners; no expansion.
std::vector<std::string> words(std::string_view text) {
  std::vector<std::string> result; std::string word; char quote = 0; bool started = false;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '\\' && quote != '\'') { check(i + 1 < text.size(), "dangling command escape"); word += text[++i]; started = true; }
    else if (quote) { if (c == quote) quote = 0; else word += c; }
    else if (c == '\'' || c == '"') { quote = c; started = true; }
    else if (c == ' ' || c == '\n' || c == '\r' || c == '\t') { if (started) { result.push_back(word); word.clear(); started = false; } }
    else { word += c; started = true; }
  }
  check(!quote, "unclosed command quote"); if (started) result.push_back(word); return result;
}
std::map<std::string, std::string> metadata(const std::string& text) {
  std::istringstream in(text); std::string l; std::map<std::string, std::string> out;
  while (std::getline(in, l)) if (!l.empty() && l[0] != '#') {
    const auto eq = l.find('='); check(eq != l.npos, "bad expect.txt line"); out[l.substr(0,eq)] = l.substr(eq+1);
  }
  return out;
}
struct Temp {
  std::filesystem::path path;
  Temp() {
    path = std::filesystem::current_path() / (".distill-pipeline-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(std::filesystem::create_directory(path), "cannot create tests directory");
  }
  ~Temp() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};
struct KeptWork {
  std::filesystem::path path;
  ~KeptWork() { if (!path.empty()) { std::error_code ec; std::filesystem::remove_all(path, ec); } }
};
struct ScopedPath {
  std::optional<std::string> original;
  ScopedPath() { if (const auto* p = std::getenv("PATH")) original = p; }
  static void set(const std::string& value) {
#ifdef _WIN32
    _putenv_s("PATH", value.c_str());
#else
    setenv("PATH", value.c_str(), 1);
#endif
  }
  ~ScopedPath() {
    if (original) set(*original);
    else {
#ifdef _WIN32
      _putenv_s("PATH", "");
#else
      unsetenv("PATH");
#endif
    }
  }
};
// The injected runner simulates a missed inline, or a resolver that corrupted
// an ordinary return token AFTER deciding correctly. sameDecisions alone cannot
// catch the latter: the production preprocess check must reject its checked bytes.
void inject(const ProcessSpec& spec, const std::string& fault) {
  for (const auto& a : spec.argv) {
    const auto p = pathFromUtf8(a);
    if (p.filename() == "raw.cpp" && fault == "missed-inline")
      write(p, "#line 1 \"main.cpp\"\n#include \"only.h\"\nint main(){return only;}\n");
    if (p.filename() == "flat.cpp" && fault == "resolver") {
      auto s = read(p); const auto pos = s.find("return 0");
      if (pos != s.npos) { s.replace(pos, 8, "return 1"); write(p, s); }
    }
  }
}
int run(int argc, char** argv) {
  const auto gcc = testing::testGcc();
  if (!gcc) { std::cout << "SKIP pipeline cases: GCC unavailable\n"; return 0; }
  const bool cli = argc == 3 && std::string_view(argv[1]) == "--cli";
  const bool worker = argc == 3 && std::string_view(argv[1]) == "--worker";
  const auto workerPath = worker ? std::filesystem::absolute(pathFromUtf8(argv[2])) : std::filesystem::path{};
  const auto binary = cli ? std::filesystem::absolute(pathFromUtf8(argv[2])) : std::filesystem::path{};
  Temp temp;
  auto evaluator = testing::testEvaluator(temp.path);
  auto fixture = take(testing::captureProfile(*gcc, temp.path));
  int passed = 0, failed = 0;
  std::vector<std::filesystem::path> dirs;
  for (const auto& d : std::filesystem::directory_iterator(DISTILL_PIPELINE_CASES)) if (d.is_directory()) dirs.push_back(d.path());
  std::sort(dirs.begin(), dirs.end());
  for (const auto& dir : dirs) {
    const auto name = pathToUtf8(dir.filename());
    try {
      const auto cwd = temp.path / dir.filename(); std::filesystem::create_directory(cwd);
      for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
        const auto target = cwd / e.path().lexically_relative(dir);
        if (e.is_directory()) std::filesystem::create_directory(target);
        else write(target, read(e.path()));
      }
      const auto expected = metadata(read(cwd / "expect.txt"));
      auto value = [&](const char* key) { const auto f = expected.find(key); return f == expected.end() ? std::string{} : f->second; };
      if (value("requires") == "linux" && fixture.triple.find("linux") == fixture.triple.npos) {
        std::cout << "SKIP " << name << ": requires Linux GCC\n"; continue;
      }
      if (value("prune") == "yes" && !cli && !worker) {
        std::cout << "SKIP " << name << ": requires SDK pruning worker\n"; continue;
      }
      if (cli && value("api") == "yes") {
        std::cout << "SKIP " << name << ": internal sample comparison API (D4 CLI pending)\n"; continue;
      }
      auto command = words(uncomment(read(cwd / "cmd.txt")));
      check(!command.empty(), "empty cmd.txt"); command[0] = pathToUtf8(*gcc);
      std::optional<Profile> profile;
      if (value("profile") == "fixture") profile = fixture;
      else if (!value("profile").empty()) profile = take(lookupProfile(value("profile")));
      if (profile && value("check") == "present") profile->check = {pathToUtf8(*gcc)};
      ScopedPath searchPath;
      if (profile && value("check") == "wrong-target") {
        const auto shim = cwd / "wrong-target-g++";
        std::ostringstream py;
        py << "#!/usr/bin/env python3\n# SPDX-License-Identifier: GPL-3.0-or-later\nimport os, sys\n"
           << "if '-dumpmachine' in sys.argv:\n print('x86_64-w64-mingw32')\n sys.exit(0)\n"
           << "os.execv(" << std::quoted(pathToUtf8(*gcc)) << ", [" << std::quoted(pathToUtf8(*gcc)) << "] + sys.argv[1:])\n";
        write(shim, py.str()); std::filesystem::permissions(shim, std::filesystem::perms::owner_all);
        ScopedPath::set(pathToUtf8(cwd) + ":" + searchPath.original.value_or(""));
        profile->check = {"wrong-target-g++"};
        profile->link = {"-Wl,--stack=268435456"};
      }
      const auto wantedLevel = value("verification") == "fixture" ? (fixture.reproduced ? "target" : "local") : value("verification");
      std::string report, artifact, level, kind, diagnostic, bytes;
      int code;
      KeptWork kept;
      if (cli) {
        if (value("profile") == "fixture") {
          auto text = read(temp.path / "fixture.toml");
          if (value("check") == "present" || value("check") == "wrong-target") {
            const auto begin = text.find("check = "), end = text.find('\n', begin);
            std::ostringstream line; line << "check = [" << std::quoted(profile->check.front()) << "]";
            text.replace(begin, end - begin, line.str());
          }
          if (value("check") == "wrong-target") text += "link = ['-Wl,--stack=268435456']\n";
          write(cwd / "fixture.toml", text);
          write(cwd / "fixture-predefined.txt", read(temp.path / "fixture-predefined.txt"));
        }
        if (!value("fault").empty()) {
          // Same injection at the process boundary for the real CLI, without a
          // product flag or environment backdoor. Linux CI supplies Python 3.
          const auto shim = cwd / "compiler-shim";
          std::ostringstream py;
          py << "#!/usr/bin/env python3\n# SPDX-License-Identifier: GPL-3.0-or-later\nimport os, pathlib, sys\n";
          py << "fault = " << std::quoted(value("fault")) << "\n";
          py << "for arg in sys.argv[1:]:\n p = pathlib.Path(arg)\n if p.name == 'raw.cpp' and fault == 'missed-inline':\n  p.write_text('#line 1 \"main.cpp\"\\n#include \"only.h\"\\nint main(){return only;}\\n')\n if p.name == 'flat.cpp' and fault == 'resolver':\n  p.write_text(p.read_text().replace('return 0', 'return 1', 1))\n if p.name == 'pruned.cpp' and fault == 'prune-build':\n  p.write_text(p.read_text().replace('int chosen(){return 7;}', '', 1))\n";
          py << "os.execv(" << std::quoted(pathToUtf8(*gcc)) << ", [" << std::quoted(pathToUtf8(*gcc)) << "] + sys.argv[1:])\n";
          write(shim, py.str()); std::filesystem::permissions(shim, std::filesystem::perms::owner_all);
          command[0] = pathToUtf8(shim);
        }
        ProcessSpec s; s.cwd = cwd;
        s.argv = {pathToUtf8(binary), "--report", "report.txt", "-o", "submit.cpp"};
        if (value("prune") != "yes") s.argv.push_back("--no-prune");
        if (value("strip") == "yes") s.argv.push_back("--strip-comments");
        if (!value("keep").empty()) { s.argv.push_back("--keep"); s.argv.push_back(value("keep")); }
        if (value("strict") == "yes") s.argv.push_back("--strict");
        if (profile) { s.argv.push_back("--judge"); s.argv.push_back(value("profile") == "fixture" ? "fixture.toml" : value("profile")); }
        s.argv.insert(s.argv.end(), command.begin(), command.end());
        s.timeout = std::chrono::seconds(30);
        const auto r = take(runProcess(s)); check(!r.timedOut && !r.stdoutTruncated && !r.stderrTruncated, "CLI limit");
        code = r.exitCode; diagnostic = r.stderrText; report = read(cwd / "report.txt");
        if (value("kept") == "yes") {
          const std::string marker = "work files kept at ";
          const auto begin = diagnostic.find(marker);
          check(begin != diagnostic.npos, "CLI raw failure did not report kept work");
          const auto pathBegin = begin + marker.size(), end = diagnostic.find('\n', pathBegin);
          kept.path = pathFromUtf8(diagnostic.substr(pathBegin, end - pathBegin));
        }
        if (code != 1) bytes = read(cwd / "submit.cpp");
        else check(!std::filesystem::exists(cwd / "submit.cpp"), "failed CLI wrote output");
        const auto line = "distill: wrote " + value("artifact") + " (" + wantedLevel + "), runtime not requested";
        if (code != 1) check(diagnostic.find(line) != diagnostic.npos, "wrong result line: " + diagnostic);
        if (code != 1 && value("prune") == "yes") {
          // The public CLI still reports runtime not requested. The SDK runner
          // independently compares its exact published bytes against a second
          // real-CLI flat artifact under the same selected local configuration.
          auto referenceSpec = s;
          referenceSpec.argv.insert(referenceSpec.argv.begin()+1,"--no-prune");
          for (auto& arg : referenceSpec.argv) if (arg == "submit.cpp") arg = "flat-reference.cpp";
          const auto flatRun = take(runProcess(referenceSpec));
          check(!flatRun.timedOut && (flatRun.exitCode == 0 || flatRun.exitCode == 3),
                "flat CLI reference failed: " + flatRun.stderrText);
          write(cwd / "sample.in",value("sample"));
          auto compile = take(flatCommand(take(parseCompileCommand(command,cwd)),profile ? &*profile : nullptr,
                                          pathFromUtf8(command.front()),false));
          const auto runDir = temp.path / ("cli-runs-" + name); std::filesystem::create_directory(runDir);
          std::vector<ProcessResult> compared;
          for (const auto* source : {"flat-reference.cpp","submit.cpp"}) {
            ProcessSpec build; build.cwd = runDir; build.timeout = std::chrono::seconds(20);
            const auto program = runDir / (std::string(source)+".exe");
            build.argv = compile.buildArgv(cwd / source,program,true,{},true);
            const auto built = take(runProcess(build));
            check(!built.timedOut && !built.exitCode && !built.signal,"published CLI artifact build failed: " + built.stderrText);
            ProcessSpec run; run.cwd = runDir; run.argv = {pathToUtf8(program)};
            run.stdinFile = cwd / "sample.in"; run.timeout = std::chrono::seconds(10);
            auto output = take(runProcess(run));
            check(!output.timedOut && !output.signal && !output.stdoutTruncated && !output.stderrTruncated,
                  "published CLI artifact run failed: " + output.stderrText);
            compared.push_back(std::move(output));
          }
          check(compared[0].exitCode == compared[1].exitCode && compared[0].stdoutText == compared[1].stdoutText,
                "published CLI artifact differs from flat on sample.in");
        }
      } else {
        auto cmd = take(parseCompileCommand(command, cwd));
        DistillOptions o; o.noPrune = value("prune") != "yes"; o.pruningWorker = workerPath;
        o.stripComments = value("strip") == "yes";
        if (!value("keep").empty()) o.keep = {value("keep")};
        if (value("prune") == "yes" || value("samples") == "yes") {
          write(cwd / "sample.in",value("sample")); o.samples = {{"sample.in",cwd / "sample.in"}};
        }
        o.profile = profile ? &*profile : nullptr; o.strict = value("strict") == "yes";
        if (value("temporary") == "override") {
          o.temporaryRoot = temp.path / ("work-" + name);
          std::filesystem::create_directory(*o.temporaryRoot);
        }
        const auto workRoot = o.temporaryRoot.value_or(std::filesystem::temp_directory_path());
        std::map<std::string, std::string> builtInputs;
        std::filesystem::path workPath;
        auto result = take(distillProgram(cmd, o, *evaluator, [&](const ProcessSpec& spec) {
          for (const auto& arg : spec.argv) {
            const auto p = pathFromUtf8(arg);
            if (p.filename() != "raw.cpp" && p.filename() != "flat.cpp" && p.filename() != "pruned.cpp") continue;
            workPath = p.parent_path();
            check(std::filesystem::equivalent(workPath.parent_path(), workRoot), "work is outside requested temp root");
            check(spec.cwd == workPath, "generated check uses user cwd");
            for (const auto& word : spec.argv)
              check(!word.starts_with("-I") && !word.starts_with("-iquote"), "generated check retains user include path");
          }
          inject(spec, value("fault"));
          if (std::find(spec.argv.begin(), spec.argv.end(), "-E") == spec.argv.end())
            for (const auto& arg : spec.argv) {
              const auto p = pathFromUtf8(arg);
              if (p.filename() == "raw.cpp") builtInputs["raw"] = read(p);
              if (p.filename() == "flat.cpp") builtInputs["flat"] = read(p);
              if (p.filename() == "pruned.cpp") builtInputs["pruned"] = read(p);
            }
          auto process = runProcess(spec);
          // Test-only bad analysis: remove a user-pasted callee's positive AST
          // and dependent-name edges. Raw tokens deliberately cannot name it.
          if ((value("fault") == "prune-build" || value("fault") == "prune-run") &&
              spec.argv.size() == 3 && spec.argv[1] == "--internal-prune" && process.ok()) {
            auto model = take(parsePruningModel(process.value().stdoutText));
            FamilyId victim = noFamily;
            for (FamilyId i = 0; i < model.families.size(); ++i) if (model.families[i].names.contains("chosen")) victim = i;
            check(victim != noFamily,"test fault could not find chosen declaration");
            for (auto& f : model.families) {
              std::erase(f.edges,victim); std::erase(f.associatedTypes,victim); f.dependentNames.erase("chosen");
            }
            std::erase_if(model.unions,[&](auto u) { return u.first == victim || u.second == victim; });
            auto& f = model.families[victim]; f.root.clear(); f.pinned.clear(); f.neverRemoved.clear();
            process.value().stdoutText = take(serializePruningModel(model));
          }
          return process;
        }));
        code = result.exitCode; artifact = result.artifact; level = result.verification; report = result.report;
        if (result.keptWork) kept.path = *result.keptWork;
        if (!workPath.empty() && !result.keptWork) check(!std::filesystem::exists(workPath), "successful work was not removed");
        if (value("check") == "wrong-target") {
          bool skipped = false;
          for (const auto& c : result.checks) if (c.configuration && c.configuration->kind == "check") {
            skipped |= c.status.starts_with("check compiler skipped:");
            check(c.satisfied.empty(), "incompatible check compiler was evaluated");
          }
          check(skipped, "incompatible check compiler was not recorded as skipped");
        }
        diagnostic = result.diagnostic; bytes = result.bytes;
        if (value("prune") == "yes" && code != 1)
          check(result.runtime == "compared 1 samples", "missing internal runtime comparison: " + result.runtime + "\n" + report);
        if (code != 1) {
          check(artifact == value("artifact"), "wrong artifact: " + artifact + "\n" + report);
          check(level == wantedLevel, "wrong verification: " + level + "\n" + report);
          if (artifact == "flat" || artifact == "pruned") for (const auto& c : result.checks) if (c.artifact == artifact && c.status == "passed") kind = c.configuration->kind;
          if (artifact == "raw") kind = "local";
          check(kind == value("configuration"), "wrong configuration kind: " + kind);
          // The returned bytes are exactly the checked output; no renderer runs afterwards.
          check(builtInputs.contains(artifact) && bytes == builtInputs.at(artifact), "returned bytes differ from compiled bytes");
        } else {
          check(bytes.empty(), "failed pipeline returned output");
          if (value("kept") == "yes") {
            check(result.keptWork.has_value(), "raw failure did not keep files");
            check(std::filesystem::exists(*result.keptWork / "raw.cpp"), "kept raw missing");
            check(diagnostic.find(pathToUtf8(*result.keptWork)) != diagnostic.npos, "kept work not reported");
          }
        }
      }
      check(code == std::stoi(value("exit")), "wrong exit: " + std::to_string(code) + "\n" + diagnostic);
      for (const auto& [key, line] : expected) if (key == "report" || key.starts_with("report."))
        check(report.find(line) != report.npos, "missing report line: " + line + "\n" + report);
      for (const auto& [key,text] : expected) {
        if (key == "contains" || key.starts_with("contains.")) check(bytes.find(text) != bytes.npos,"missing output: " + text + "\n" + bytes);
        if (key == "absent" || key.starts_with("absent.")) check(bytes.find(text) == bytes.npos,"unexpected output: " + text + "\n" + bytes);
      }
      if (!value("reason").empty()) check((report + diagnostic).find(value("reason")) != std::string::npos, "missing reason: " + value("reason"));
      for (const auto& e : std::filesystem::directory_iterator(cwd))
        check(!pathToUtf8(e.path().filename()).starts_with(".distill-"), "work files appeared in user cwd");
      if (value("kept") == "yes") {
        check(!kept.path.empty() && std::filesystem::exists(kept.path / "raw.cpp"), "raw failure removed work");
        check(std::filesystem::equivalent(kept.path.parent_path(), std::filesystem::temp_directory_path()), "kept work is outside OS temp root");
      }
      ++passed; std::cout << "PASS " << name << '\n';
    } catch (const std::exception& e) { ++failed; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
  }
  std::cout << passed << " passed, " << failed << " failed\n"; return failed ? 1 : 0;
}
}
int main(int argc, char** argv) {
  try { return run(argc, argv); }
  catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
