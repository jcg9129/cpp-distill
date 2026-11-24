// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/pipeline/pipeline.h"
#include "core/command/options.h"
#include "core/path.h"
#include "evaluate.h"
#include "profile.h"
#include "library_features.h"
#include "core/render/render.h"
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
#include "checker_cases.h"
#include "caide_cases.h"
int run(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--caide-assertions") return caideAssertionModels();
  const auto gcc = testing::testGcc();
  if (!gcc && argc == 4 && std::string_view(argv[1]) == "--caide")
    throw std::runtime_error("caide assertions require GCC");
  if (!gcc) { std::cout << "SKIP pipeline cases: GCC unavailable\n"; return 0; }
  if (argc == 4 && std::string_view(argv[1]) == "--caide")
    return caideCase(std::filesystem::absolute(pathFromUtf8(argv[3])),
                     std::filesystem::absolute(pathFromUtf8(argv[2])),*gcc);
  const bool models = argc == 2 && std::string_view(argv[1]) == "--checker-models";
  const bool d3c = argc == 4 && std::string_view(argv[3]) == "--d3c";
  const bool d3d = argc == 4 && std::string_view(argv[3]) == "--d3d";
  const bool cli = argc >= 3 && std::string_view(argv[1]) == "--cli";
  const bool worker = argc >= 3 && std::string_view(argv[1]) == "--worker";
  const auto workerPath = models ? std::filesystem::current_path() / "synthetic-worker" : worker ? std::filesystem::absolute(pathFromUtf8(argv[2])) : std::filesystem::path{};
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
    std::string caseDiagnostics, caseReport;
    try {
      const auto cwd = temp.path / dir.filename(); std::filesystem::create_directory(cwd);
      for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
        const auto target = cwd / e.path().lexically_relative(dir);
        if (e.is_directory()) std::filesystem::create_directory(target);
        else write(target, read(e.path()));
      }
      const auto expected = metadata(read(cwd / "expect.txt"));
      auto value = [&](const char* key) { const auto f = expected.find(key); return f == expected.end() ? std::string{} : f->second; };
      if (models && value("checker").empty()) continue;
      if (!models && (cli || worker)) {
        if (d3c != (value("milestone") == "D3c") || d3d != (value("milestone") == "D3d")) continue;
      } else if (!models && value("milestone") == "D3c") continue;
      if ((value("requires") == "formatter" || value("requires") == "chrono_parse") &&
          !take(testing::libraryFeature(value("requires"),*gcc,temp.path))) {
        std::cout << "SKIP " << name << ": local library lacks " << value("requires") << '\n'; continue;
      }
      if (value("requires") == "lp64" && !fixture.triple.starts_with("x86_64-") && !fixture.triple.starts_with("aarch64-")) {
        std::cout << "SKIP " << name << ": requires LP64\n"; continue;
      }
      if (value("requires") == "lp64" && fixture.triple.find("mingw") != fixture.triple.npos) {
        std::cout << "SKIP " << name << ": requires LP64\n"; continue;
      }
      if (value("requires") == "linux" && fixture.triple.find("linux") == fixture.triple.npos) {
        std::cout << "SKIP " << name << ": requires Linux GCC\n"; continue;
      }
      if (value("requires") == "reproduced-linux" &&
          (!fixture.reproduced || fixture.triple.find("linux") == fixture.triple.npos)) {
        std::cout << "SKIP " << name << ": requires reproduced Linux fixture\n"; continue;
      }
      if (value("requires") == "x86_64" && !fixture.triple.starts_with("x86_64-")) {
        std::cout << "SKIP " << name << ": requires x86_64 GCC\n"; continue;
      }
      if (value("prune") == "yes" && !cli && !worker && !models) {
        std::cout << "SKIP " << name << ": requires SDK pruning worker\n"; continue;
      }
      if (cli && !value("checker").empty()) {
        std::cout << "SKIP " << name << ": requires direct analysis mutation\n"; continue;
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
          auto text = read(fixture.provenance.file);
          if (value("check") == "present" || value("check") == "wrong-target") {
            const auto begin = text.find("check = "), end = text.find('\n', begin);
            std::ostringstream line; line << "check = [" << std::quoted(profile->check.front()) << "]";
            text.replace(begin, end - begin, line.str());
          }
          if (value("check") == "wrong-target") text += "link = ['-Wl,--stack=268435456']\n";
          write(cwd / fixture.provenance.file.filename(), text);
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
          // The same test-only compiler boundary hook exercises CLI runtime
          // fallback; no fault controls are exposed by the product.
          py << "for arg in sys.argv[1:]:\n p = pathlib.Path(arg)\n if p.name == 'pruned.cpp' and fault == 'prune-run':\n  p.write_text(p.read_text().replace('int chosen(int){return 7;}', '', 1))\n";
          py << "os.execv(" << std::quoted(pathToUtf8(*gcc)) << ", [" << std::quoted(pathToUtf8(*gcc)) << "] + sys.argv[1:])\n";
          write(shim, py.str()); std::filesystem::permissions(shim, std::filesystem::perms::owner_all);
          command[0] = pathToUtf8(shim);
        }
        ProcessSpec s; s.cwd = cwd;
        s.argv = {pathToUtf8(binary), "--report", "report.txt", "-o", "submit.cpp"};
        if (value("prune") != "yes" && value("prune") != "diagnostics") s.argv.push_back("--no-prune");
        if (value("strip") == "yes") s.argv.push_back("--strip-comments");
        if (value("lines") == "no") s.argv.push_back("--no-line-markers");
        if (value("compact") == "yes") s.argv.push_back("--compact-whitespace");
        if (!value("no-assume").empty()) s.argv.push_back("--no-assume=" + value("no-assume"));
        if (!value("keep").empty()) { s.argv.push_back("--keep"); s.argv.push_back(value("keep")); }
        if (value("strict") == "yes") s.argv.push_back("--strict");
        if (profile) { s.argv.push_back("--judge"); s.argv.push_back(value("profile") == "fixture" ? pathToUtf8(fixture.provenance.file.filename()) : value("profile")); }
        const bool samples = value("prune") == "yes" || value("samples") == "yes";
        if (samples) {
          write(cwd / "sample.in",value("sample"));
          s.argv.insert(s.argv.end(),{"--verify-run","sample.in"});
        }
        s.argv.insert(s.argv.end(), command.begin(), command.end());
        s.timeout = std::chrono::seconds(30);
        const auto r = take(runProcess(s)); caseDiagnostics += r.stderrText;
        check(!r.timedOut && !r.stdoutTruncated && !r.stderrTruncated, "CLI limit");
        code = r.exitCode; diagnostic = r.stderrText; report = read(cwd / "report.txt");
        if (profile) {
          const auto selector = value("profile") == "fixture" ? pathToUtf8(fixture.provenance.file.filename()) : value("profile");
          const auto selection = "judge: " + selector + " -> " + profile->name + "\n";
          check(diagnostic.find(selection) != diagnostic.npos, "CLI missing judge selection on stderr");
          check(report.find(selection) != report.npos, "report missing judge selection");
          check(report.find("judge digest: sha256:") != report.npos, "report missing profile digest");
        }
        if (value("kept") == "yes") {
          const std::string marker = "work files kept at ";
          const auto begin = diagnostic.find(marker);
          check(begin != diagnostic.npos, "CLI raw failure did not report kept work");
          const auto pathBegin = begin + marker.size(), end = diagnostic.find('\n', pathBegin);
          kept.path = pathFromUtf8(diagnostic.substr(pathBegin, end - pathBegin));
        }
        if (code != 1) bytes = read(cwd / "submit.cpp");
        else check(!std::filesystem::exists(cwd / "submit.cpp"), "failed CLI wrote output");
        const auto line = "distill: wrote " + value("artifact") + " (" + wantedLevel + "), runtime " + (samples ? "compared 1 samples" : "not requested");
        if (code != 1) {
          std::istringstream lines(diagnostic); std::string actual; bool found = false;
          while (std::getline(lines,actual)) {
            if (!actual.empty() && actual.back() == '\r') actual.pop_back();
            if (actual == line) found = true;
          }
          check(found, "wrong result line: " + diagnostic);
        }
        if (code != 1 && value("prune") == "yes") {
          // Independently compare exact published bytes with checked flat bytes.
          auto referenceSpec = s;
          referenceSpec.argv.insert(referenceSpec.argv.begin()+1,"--no-prune");
          for (auto& arg : referenceSpec.argv) if (arg == "submit.cpp") arg = "flat-reference.cpp";
          const auto flatRun = take(runProcess(referenceSpec));
          check(!flatRun.timedOut && (flatRun.exitCode == 0 || flatRun.exitCode == 3),
                "flat CLI reference failed: " + flatRun.stderrText);
          write(cwd / "sample.in",value("sample"));
          auto compile = take(flatCommand(take(parseCompileCommand(command,cwd)),profile ? &*profile : nullptr,
                                          pathFromUtf8(command.front()),false));
          // Like the pipeline, keep system searches meaningful after changing
          // cwd for the independent build of the published artifact.
          for (auto& arg : compile.args) if (arg.option == "-isystem" || arg.option == "-L") {
            arg.value = pathToUtf8(cwd / pathFromUtf8(arg.value)); arg.spelling = {arg.option,arg.value};
          }
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
        // Diagnostic-only fixtures exercise preparation/retention without an SDK worker.
        DistillOptions o; o.noPrune = value("prune") != "yes" && value("prune") != "diagnostics"; o.pruningWorker = workerPath;
        if (!value("checker").empty()) o.temporaryRoot = temp.path;
        o.stripComments = value("strip") == "yes"; o.noLineMarkers = value("lines") == "no"; o.compactWhitespace = value("compact") == "yes";
        if (!value("keep").empty()) o.keep = {value("keep")};
        if (!value("no-assume").empty()) {
          const auto flags = take(parseDistillOptions({"--no-assume=" + value("no-assume"),"main.cpp"}));
          o.assumeInstantiationEffects = flags.assumeInstantiationEffects;
          o.assumeLibrarySpelledMembers = flags.assumeLibrarySpelledMembers;
          o.assumeLibraryMacroValues = flags.assumeLibraryMacroValues;
          o.assumeUnspelledFunctions = flags.assumeUnspelledFunctions;
          o.assumeIdempotentStandardHeaders = flags.assumeIdempotentStandardHeaders;
          o.assumeUnobservedGuardMacros = flags.assumeUnobservedGuardMacros;
        }
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
        ProcessRunner caseRunner = [&](const ProcessSpec& spec) -> Result<ProcessResult> {
          if (models && spec.argv.size() == 3 && spec.argv[1] == "--internal-prune") {
            auto m = take(parsePruningModel(read(pathFromUtf8(spec.argv[2]))));
            for (const auto& [key,text] : expected) if (key.starts_with("model.")) {
              const auto start = m.text.find(text); check(start != m.text.npos,"synthetic family text missing: " + key);
              PruneFamily f; f.label = key.substr(6); f.names = {f.label.substr(f.label.rfind("::") == f.label.npos ? 0 : f.label.rfind("::")+2)};
              f.ranges = {{start,start+text.size()}}; f.root = "test-only model root";
              m.families.push_back(std::move(f));
            }
            ProcessResult r; r.exitCode = 0; r.stdoutText = take(serializePruningModel(m)); return r;
          }
          auto r = runProcess(spec);
          if (!r.ok() || !r.value().stderrText.empty() || spec.argv.size() == 1) {
            std::ostringstream detail; detail << "cwd=" << spec.cwd << " argv=";
            for (const auto& a : spec.argv) detail << ' ' << std::quoted(a);
            detail << '\n' << (r.ok() ? r.value().stderrText : r.error().message) << '\n';
            if (r.ok() && spec.argv.size() == 1) detail << "exit=" << r.value().exitCode << " stdout:\n" << r.value().stdoutText << '\n';
            caseDiagnostics += detail.str();
          }
          return r;
        };
        auto result = take(distillProgram(cmd, o, *evaluator, [&](const ProcessSpec& spec) {
          for (const auto& arg : spec.argv) {
            const auto p = pathFromUtf8(arg);
            if (p.filename() != "raw.cpp" && p.filename() != "flat.cpp" && p.filename() != "pruned.cpp" && p.filename() != "unmarked.cpp" && p.filename() != "compact.cpp") continue;
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
              if (p.filename() == "unmarked.cpp") builtInputs["unmarked"] = read(p);
              if (p.filename() == "compact.cpp") builtInputs["compact"] = read(p);
            }
          auto process = caseRunner(spec);
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
        caseReport = result.report;
        if (!value("checker").empty())
          checkerCase(result,value("victim"),value("checker"),value("mutation"),value("witness"),cmd,o,*evaluator,caseRunner);
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
        if (code != 1)
          check(result.resultLine == "distill: wrote " + value("artifact") + " (" + wantedLevel + "), runtime " + result.runtime,
                "wrong result line: " + result.resultLine);
        if (value("prune") == "yes" && code != 1)
          check(result.runtime == "compared 1 samples", "missing internal runtime comparison: " + result.runtime + "\n" + report);
        if (code != 1) {
          check(artifact == value("artifact"), "wrong artifact: " + artifact + "\n" + report);
          check(level == wantedLevel, "wrong verification: " + level + "\n" + report);
          if (artifact == "flat" || artifact == "pruned") for (const auto& c : result.checks) if (c.artifact == artifact && c.status == "passed") kind = c.configuration->kind;
          if (artifact == "raw") kind = "local";
          check(kind == value("configuration"), "wrong configuration kind: " + kind);
          // The returned bytes are exactly the checked output; no renderer runs afterwards.
          const auto written = result.compactWhitespace == "compacted" ? std::string("compact") : result.lineMarkers == "omitted" ? std::string("unmarked") : artifact;
          check(builtInputs.contains(written) && bytes == builtInputs.at(written), "returned bytes differ from compiled bytes");
          check((value("lines") == "no" || value("compact") == "yes") != result.lineMarkers.empty(), "line-marker outcome without --no-line-markers or missing");
        } else {
          check(bytes.empty(), "failed pipeline returned output");
          if (value("kept") == "yes") {
            check(result.keptWork.has_value(), "raw failure did not keep files");
            check(std::filesystem::exists(*result.keptWork / "raw.cpp"), "kept raw missing");
            check(diagnostic.find(pathToUtf8(*result.keptWork)) != diagnostic.npos, "kept work not reported");
          }
        }
      }
      caseReport = report;
      check(code == std::stoi(value("exit")), "wrong exit: " + std::to_string(code) + "\n" + diagnostic);
      for (const auto& [key, line] : expected) if (key == "report" || key.starts_with("report."))
        check(report.find(line) != report.npos, "missing report line: " + line + "\n" + report);
      for (const auto& [key,text] : expected) {
        if (key == "diagnostic" || key.starts_with("diagnostic.")) check(diagnostic.find(text) != diagnostic.npos,"missing diagnostic: " + text);
        if (key.starts_with("count.")) {
          std::size_t count = 0, at = 0;
          while ((at = bytes.find(text,at)) != bytes.npos) { ++count; at += text.size(); }
          check(count == std::stoul(key.substr(6)),"wrong output count: " + text + "\n" + bytes);
        }
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
    } catch (const std::exception& e) { ++failed; std::cerr << "FAIL " << name << ": " << e.what() << '\n' << caseReport << caseDiagnostics; }
  }
  std::cout << passed << " passed, " << failed << " failed\n"; return failed ? 1 : 0;
}
}
int main(int argc, char** argv) {
  try { return run(argc, argv); }
  catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
