// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/prune/extract.h"
#include "core/command/options.h"
#include "core/path.h"
#include "core/report/prune.h"
#include "core/render/render.h"
#include "core/temporary.h"
#include "library_features.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
namespace {
using namespace distill;
template<class T> T take(Result<T> r) { if (!r.ok()) throw Error{r.error().message}; return std::move(r.value()); }
std::string read(const std::filesystem::path& p) { std::ifstream f(p,std::ios::binary); if (!f) throw Error{"cannot read " + pathToUtf8(p)}; return {std::istreambuf_iterator<char>(f),{}}; }
std::string trim(std::string s) { auto a = s.find_first_not_of(" \t\r"); if (a == s.npos) return {}; return s.substr(a,s.find_last_not_of(" \t\r")-a+1); }
}
int main(int argc, char** argv) {
  if (argc != 2) return 1;
  try {
    const auto cwd = std::filesystem::current_path();
    TemporaryDirectory work(".prune-cases-",cwd);
    auto command = take(parseCompileCommand({"g++","-std=gnu++20","empty.cpp"},work.path));
    { std::ofstream f(work.path / "empty.cpp"); }
    ProbeOptions po; po.temporaryRoot = work.path;
    const auto configuration = take(probeConfiguration(command,"selected flat",po));
    std::vector<std::filesystem::path> cases;
    for (const auto& d : std::filesystem::directory_iterator(DISTILL_PRUNE_CASES)) if (d.is_directory()) {
      cases.push_back(d.path() / "expect.txt");
      if (std::filesystem::exists(d.path() / "no-assume.txt")) cases.push_back(d.path() / "no-assume.txt");
    }
    std::sort(cases.begin(),cases.end()); std::size_t count = 0, failed = 0;
    for (const auto& expectation : cases) {
      const auto path = expectation.parent_path();
      PruningModel model; PruningResult retained; bool caseFailed = false; std::string workerDiagnostics;
      auto fail = [&](const std::string& why) {
        std::cerr << "FAIL " << path.filename() << "/" << expectation.filename() << ": " << why << '\n'; caseFailed = true;
      };
      try {
        ResolveResult flat; flat.text = read(path / "main.cpp");
        const auto name = pathToUtf8(path.filename());
        const auto feature = name.starts_with("formatter") ? "formatter" : name == "chrono_parse" ? "chrono_parse" : "";
        const bool native = !*feature || take(testing::libraryFeature(feature,"g++",work.path));
        auto scan = take(scanDirectives(flat.text));
        std::size_t lines = 1 + std::count(flat.text.begin(),flat.text.end(),'\n');
        for (std::size_t i = 1; i <= lines; ++i) flat.lineMap.push_back(SourceLocation{pathToUtf8(path / "main.cpp"),i});
        if (std::filesystem::exists(path / "generated-lines.txt")) {
          std::istringstream in(read(path / "generated-lines.txt")); std::string l;
          while (std::getline(in,l)) if (!l.empty() && !l.starts_with('#')) flat.lineMap.at(std::stoul(l)-1).reset();
        }
        std::vector<std::string> args;
        std::istringstream metadata(read(expectation)); std::string entry;
        while (std::getline(metadata,entry)) if (entry.starts_with("args |")) {
          std::istringstream words(entry.substr(6)); std::string word;
          while (words >> word) args.push_back(word);
        }
        args.push_back("empty.cpp");
        const auto cli = take(parseDistillOptions(args));
        model = take(preparePruning(configuration,{},flat,&configuration.facts,cli.keep));
        model.assumeInstantiationEffects = cli.assumeInstantiationEffects;
        if (*feature) {
          model.dialect = "gnu++23";
          for (auto& arg : model.parseArgv) if (arg == "-std=gnu++20") arg = "-std=gnu++23";
          if (!native) {
            const auto header = std::filesystem::path(DISTILL_PRUNING_SUPPORT) / (std::string(feature) == "formatter" ? "format" : "chrono");
            model.parseArgv.insert(model.parseArgv.begin(),{"-isystem",pathToUtf8(header)});
            std::cout << "MODEL " << name << ": local library lacks " << feature << "\n";
          }
        }
        PruneWorkerOptions options; options.executable = std::filesystem::absolute(argv[1]); options.temporaryRoot = work.path;
        options.runner = [&](const ProcessSpec& spec) {
          auto r = runProcess(spec);
          if (r.ok()) workerDiagnostics += r.value().stderrText;
          else workerDiagnostics += r.error().message;
          return r;
        };
        model = take(extractPruning(model,options)); retained = take(computeRetention(model));
        std::istringstream expect(read(expectation)); std::string line; bool disabled = false, a11Exclusion = false;
        while (std::getline(expect,line)) {
          if (line.empty() || line.starts_with('#')) continue;
          const auto bar = line.find('|'); if (bar == line.npos) { fail("invalid expectation: " + line); continue; }
          auto assertion = trim(line.substr(0,bar)), reason = trim(line.substr(bar+1)); bool matched = false;
          if (assertion == "args") continue;
          if (assertion == "report-absent") {
            matched = take(renderPruningReport(model,retained)).find(reason) == std::string::npos;
          } else if (assertion == "report") {
            matched = take(renderPruningReport(model,retained)).find(reason) != std::string::npos;
          } else if (assertion == "a11-exclusion") {
            // This fixture deliberately violates A11. Check the original's
            // oracle and the allowed removal, without requiring equivalence.
            a11Exclusion = true;
            matched = !reason.empty() && retained.assumptions.contains("A11") &&
                take(renderPruningReport(model,retained)).find("pruning assumption: A11 instantiation-effects\n") != std::string::npos;
          } else if (assertion == "disabled") {
            disabled = true; for (const auto& why : retained.disabledReasons) matched |= why.find(reason) != why.npos;
          } else {
            auto space = assertion.find(' ');
            if (space == assertion.npos || (assertion.substr(0,space) != "kept" && assertion.substr(0,space) != "removed")) { fail("invalid expectation: " + line); continue; }
            auto fate = assertion.substr(0,space), label = assertion.substr(space+1);
            bool found = false, allFates = true;
            for (FamilyId id = 0; id < model.families.size(); ++id) if (model.families[id].label == label) {
              found = true;
              const auto& r = retained.families[id];
              allFates &= r.kept == (fate == "kept");
              if (r.kept == (fate == "kept") && r.reason.find(reason) != r.reason.npos) matched = true;
            }
            matched &= found && allFates;
          }
          if (!matched) fail("missing " + line);
        }
        if (!disabled && !retained.disabledReasons.empty()) fail("unexpectedly disabled");
        for (const auto& why : model.disabledReasons)
          if (why.starts_with("pruning worker") || why.starts_with("invalid pruning worker result")) fail(why);
        // Every deletion range is physical, complete and disjoint; the model
        // validator independently checks bounds/overlap before retention.
        for (const auto& f : model.families) for (auto r : f.ranges) if (r.end > flat.text.size()) fail("bad range");
        // Optional runtime oracle: compare both compilers' original and pruned
        // programs, and check the expected output so a vacuous comparison fails.
        // A11 exclusions retain only the original-program oracle.
        if (std::filesystem::exists(path / "stdout.txt")) {
          const auto rendered = take(renderSource(model,&retained));
          const auto pruned = rendered.provenance.text;
          take(staticNameInvariant(model,retained,pruned));
          take(renderingInvariant(model,&retained,pruned));
          const auto expected = read(path / "stdout.txt");
          for (const auto* compiler : {"g++","clang++"}) {
            if (!resolveExecutable(compiler,work.path).ok()) {
              std::cout << "SKIP runtime " << compiler << ": unavailable\n"; continue;
            }
            for (bool original : {true,false}) {
              if (!original && a11Exclusion) continue;
              const auto source = work.path / "runtime.cpp", binary = work.path / "runtime-program";
              { std::ofstream f(source); f << (original ? flat.text : pruned); }
              ProcessSpec spec; spec.cwd = work.path; spec.timeout = std::chrono::seconds(20);
              spec.argv = {compiler,"-std=gnu++20","-O0",pathToUtf8(source),"-o",pathToUtf8(binary)};
              const auto built = take(runProcess(spec));
              if (built.exitCode || built.timedOut || built.signal) { fail(std::string(compiler) + " runtime build: " + built.stderrText); continue; }
              spec.argv = {pathToUtf8(binary)}; spec.timeout = std::chrono::seconds(5);
              const auto ran = take(runProcess(spec));
              if (ran.exitCode || ran.timedOut || ran.signal || ran.stdoutTruncated || ran.stderrTruncated ||
                  ran.stdoutText != expected || !ran.stderrText.empty())
                fail(std::string(compiler) + (original ? " original" : " pruned") + " runtime output: " + ran.stdoutText + ran.stderrText);
            }
          }
        }
      } catch (const Error& e) { fail(e.message); }
      catch (const std::exception& e) { fail(e.what()); }
      if (caseFailed) {
        ++failed;
        std::cerr << workerDiagnostics;
        for (const auto& why : model.disabledReasons) std::cerr << "  disabled: " << why << '\n';
        for (const auto& why : retained.disabledReasons)
          if (std::find(model.disabledReasons.begin(),model.disabledReasons.end(),why) == model.disabledReasons.end())
            std::cerr << "  disabled: " << why << '\n';
        for (FamilyId id = 0; id < model.families.size(); ++id)
          std::cerr << "  " << model.families[id].label << ": "
                    << (id < retained.families.size() ? retained.families[id].reason : "retention unavailable") << '\n';
      } else {
        std::cout << "PASS " << path.filename() << "/" << expectation.filename() << ": kept=" << retained.keptBytes << " removed=" << retained.removedBytes << '\n'; ++count;
      }
    }
    std::cout << count << " extraction cases passed, " << failed << " failed\n"; return failed ? 1 : 0;
  } catch (const Error& e) { std::cerr << e.message << '\n'; return 1; }
  catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
