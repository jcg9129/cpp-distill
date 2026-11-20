// SPDX-License-Identifier: GPL-3.0-or-later
#include "check.h"
#include "support.h"
#include "core/library/library.h"
#include "core/prune/extract.h"
#include "core/report/prune.h"
#include <algorithm>
#include <cstdlib>

using namespace distill;
template<class T> T take(Result<T> result) {
  if (!result.ok()) throw std::runtime_error(result.error().message);
  return std::move(result.value());
}
int main(int argc, char** argv) try {
  CHECK(std::ranges::find(embeddedLibraryData().memberNames, "scanned_only_member") != embeddedLibraryData().memberNames.end());
  CHECK(std::ranges::find(embeddedLibraryData().memberNames, "𐐀") != embeddedLibraryData().memberNames.end());
  // This TU includes no worker headers, and never references either member.
  PruningModel model; model.dialect = "c++23"; model.libraryMemberNamesComputed = true;
  model.text = "struct S {}; int scanned_only_member(); int absent_member();";
  PruneFamily owner; owner.label = "S"; owner.kind = FamilyKind::record; owner.root = "test root";
  owner.ranges = {{0, model.text.find("int scanned_only_member")}};
  model.families.push_back(owner);
  for (auto name : {"scanned_only_member", "absent_member"}) {
    PruneFamily member; member.label = name; member.kind = FamilyKind::function;
    member.memberName = name; member.memberOf = 0; member.edges = {0};
    const auto begin = model.text.find(std::string("int ") + name);
    member.ranges = {{begin, model.text.find(';', begin) + 1}};
    model.families.push_back(member);
  }
  auto result = take(computeRetention(model));
  CHECK(model.libraryMemberNames.empty());
  CHECK(result.memberDisabledReasons.empty());
  CHECK(result.families[1].kept); CHECK(!result.families[2].kept);
  CHECK(take(renderPruningReport(model, result)).find("worker headers + 1 scanned builds (generator 4)") != std::string::npos);
  if (argc == 2) {
    test::TempDirectory temp; temp.write("main.cpp", "int main(){}\n");
    const std::string gcc = std::getenv("DISTILL_TEST_GXX") ? std::getenv("DISTILL_TEST_GXX") : "g++";
    auto command = take(parseCompileCommand({gcc,"-std=c++23","main.cpp"},temp.path));
    ProbeOptions probe; probe.temporaryRoot = temp.path;
    auto config = take(probeConfiguration(command,"embedded inventory test",probe));
    ResolveResult flat;
    flat.text = "struct S { int scanned_only_member(){return 0;} int absent_member(){return 1;} }; int main(){S s;}\n";
    flat.lineMap = {SourceLocation{"main.cpp",1}};
    auto input = take(preparePruning(config,{},flat));
    PruneWorkerOptions worker; worker.executable = std::filesystem::absolute(argv[1]); worker.temporaryRoot = temp.path;
    auto extracted = take(extractPruning(input,worker));
    CHECK(extracted.libraryMemberNamesComputed);
    CHECK(extracted.libraryMemberNames.contains("scanned_only_member"));
    CHECK(!extracted.libraryMemberNames.contains("absent_member"));
    auto kept = take(computeRetention(extracted));
    CHECK(kept.disabledReasons.empty()); CHECK(kept.memberDisabledReasons.empty());
    bool found = false;
    for (FamilyId i = 0; i < extracted.families.size(); ++i) {
      if (extracted.families[i].memberName == "scanned_only_member") { CHECK(kept.families[i].kept); found = true; }
      if (extracted.families[i].memberName == "absent_member") CHECK(!kept.families[i].kept);
    }
    CHECK(found);
  }
  return test::failures ? 1 : 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
