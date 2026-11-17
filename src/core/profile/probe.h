// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/profile/profile.h"

namespace distill {
struct JudgeProbePart { std::size_t number = 1, total = 1; };
Result<JudgeProbePart> parseJudgeProbePart(std::string_view text);
struct JudgeProbeFact {
  std::string category, name;
  // Empty for comparable expansions; otherwise a presence-only explanation.
  std::string unsupported;
};
struct JudgeProbe {
  std::string source, header, manifest;
  std::vector<JudgeProbeFact> facts;
  std::size_t outputBound = 0;
};
Result<JudgeProbe> generateJudgeProbe(const Profile&, JudgeProbePart part = {});
struct JudgeCheck {
  bool passed = true;
  std::size_t checked = 0, skipped = 0;
  std::string report;
};
// Expected may contain concatenated complete parts. Judge files may each contain
// one or more parts, but their union must be exactly the complete manifest.
JudgeCheck checkJudgeProbe(const Profile&, std::string_view expected,
                          const std::vector<std::string>& outputs);
// Load and validate the expected-only wire format against the current generator.
Result<std::string> judgeProbeExpected(const Profile&);
Result<std::filesystem::path> recordJudgeObservation(const Profile&, const JudgeCheck&,
    const std::vector<std::string>& outputs, const std::filesystem::path& directory,
    std::string date = {});
std::string latestJudgeObservation(const Profile&, const ProfileFiles&);
} // namespace distill
