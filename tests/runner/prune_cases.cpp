// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/prune/extract.h"
#include "core/path.h"
#include "core/temporary.h"
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
    for (const auto& d : std::filesystem::directory_iterator(DISTILL_PRUNE_CASES)) if (d.is_directory()) cases.push_back(d.path());
    std::sort(cases.begin(),cases.end()); std::size_t count = 0;
    for (const auto& path : cases) {
      ResolveResult flat; flat.text = read(path / "main.cpp");
      auto scan = take(scanDirectives(flat.text));
      std::size_t lines = 1 + std::count(flat.text.begin(),flat.text.end(),'\n');
      for (std::size_t i = 1; i <= lines; ++i) flat.lineMap.push_back(SourceLocation{pathToUtf8(path / "main.cpp"),i});
      if (std::filesystem::exists(path / "generated-lines.txt")) {
        std::istringstream in(read(path / "generated-lines.txt")); std::string l;
        while (std::getline(in,l)) if (!l.empty() && !l.starts_with('#')) flat.lineMap.at(std::stoul(l)-1).reset();
      }
      auto model = take(preparePruning(configuration,{},flat,&configuration.facts));
      PruneWorkerOptions options; options.executable = std::filesystem::absolute(argv[1]); options.temporaryRoot = work.path;
      model = take(extractPruning(model,options)); auto retained = take(computeRetention(model));
      std::istringstream expect(read(path / "expect.txt")); std::string line; bool disabled = false;
      while (std::getline(expect,line)) {
        if (line.empty() || line.starts_with('#')) continue;
        const auto bar = line.find('|'); if (bar == line.npos) throw Error{"invalid expectation"};
        auto assertion = trim(line.substr(0,bar)), reason = trim(line.substr(bar+1)); bool matched = false;
        if (assertion == "disabled") {
          disabled = true; for (const auto& why : retained.disabledReasons) matched |= why.find(reason) != why.npos;
        } else {
          auto space = assertion.find(' '); auto fate = assertion.substr(0,space), label = assertion.substr(space+1);
          for (FamilyId id = 0; id < model.families.size(); ++id) if (model.families[id].label == label) {
            const auto& r = retained.families[id];
            if (r.kept == (fate == "kept") && r.reason.find(reason) != r.reason.npos) matched = true;
          }
        }
        if (!matched) {
          std::cerr << path.filename() << ": missing " << line << '\n';
          for (const auto& why : retained.disabledReasons) std::cerr << "  disabled: " << why << '\n';
          for (FamilyId id = 0; id < model.families.size(); ++id) std::cerr << "  " << model.families[id].label << ": " << retained.families[id].reason << '\n';
          return 1;
        }
      }
      if (!disabled && !retained.disabledReasons.empty()) throw Error{pathToUtf8(path.filename()) + ": unexpectedly disabled: " + retained.disabledReasons.front()};
      // Every deletion range is physical, complete and disjoint; the model
      // validator independently checks bounds/overlap before retention.
      for (const auto& f : model.families) for (auto r : f.ranges) if (r.end > flat.text.size()) throw Error{"bad range"};
      std::cout << "PASS " << path.filename() << ": kept=" << retained.keptBytes << " removed=" << retained.removedBytes << '\n'; ++count;
    }
    std::cout << count << " extraction cases passed\n"; return 0;
  } catch (const Error& e) { std::cerr << e.message << '\n'; return 1; }
  catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
