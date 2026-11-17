// SPDX-License-Identifier: GPL-3.0-or-later
#include "cli/judge.h"
#include "core/profile/probe.h"
#include "core/path.h"
#include <cstdlib>
#include <fstream>
#include <iostream>

namespace distill {
namespace {
template<class T> T take(Result<T> r) { if (!r.ok()) throw r.error(); return std::move(r.value()); }
}
int runJudgeDiagnostic(const CliOptions& options) {
  ProfileLookup lookup;
  for (const char* key : {"HOME", "XDG_CONFIG_HOME", "APPDATA"}) if (const auto* value = std::getenv(key)) lookup.environment[key] = value;
  auto profile = take(lookupProfile(options.judgeProbe ? *options.judgeProbe : *options.judgeCheck,lookup));
  for (const auto& warning : profile.warnings) std::cerr << warning << '\n';
  std::cerr << profileSelectionLine(profile);
  std::cerr << profileStalenessNote(profile);
  std::cerr << "judge digest: " << profile.provenance.contentDigest << "\norigin: " << profile.provenance.origin << ' ' << pathToUtf8(profile.provenance.file) << '\n';
  if (options.judgeProbe) {
    auto part = options.part ? take(parseJudgeProbePart(*options.part)) : JudgeProbePart{};
    std::cout << take(generateJudgeProbe(profile,part)).source;
    return std::cout ? 0 : 1;
  }
  std::vector<std::string> outputs;
  for (const auto& name : options.judgeOutputs) {
    std::ifstream in(pathFromUtf8(name),std::ios::binary);
    if (!in) throw Error{"cannot read judge output " + name};
    std::string bytes; char buffer[4096];
    while (in.read(buffer,sizeof(buffer)) || in.gcount()) {
      if (bytes.size() + static_cast<std::size_t>(in.gcount()) > 64 * 1024 * 1024) throw Error{"format error: judge output file exceeds 64 MiB"};
      bytes.append(buffer,static_cast<std::size_t>(in.gcount()));
    }
    if (!in.eof()) throw Error{"cannot read judge output " + name};
    outputs.push_back(std::move(bytes));
  }
  // Keep failed observations recordable, including when CI expectations are not yet shipped.
  auto check = checkJudgeProbe(profile,profile.probeExpected.value_or(""),outputs);
  std::cout << check.report;
  if (options.record) std::cerr << "recorded " << pathToUtf8(take(recordJudgeObservation(profile,check,outputs,pathFromUtf8(*options.record)))) << '\n';
  return check.passed && std::cout ? 0 : 1;
}
} // namespace distill
