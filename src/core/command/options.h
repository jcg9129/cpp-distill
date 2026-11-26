// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/result.h"
#include <optional>
#include <string>
#include <vector>

namespace distill {
struct CliOptions {
  bool help = false, version = false, strict = false, verbose = false, compilerExplicit = true;
  std::optional<std::string> output, judge, report;
  std::optional<std::string> judgeProbe, judgeCheck, part, record;
  std::vector<std::string> judgeOutputs;
  bool noPrune = false, stripComments = false, noLineMarkers = false, compactWhitespace = false;
  bool assumeLibrarySpelledNames = true;
  bool shortenNames = false;
  bool assumeInstantiationEffects = true, assumeLibrarySpelledMembers = true, assumeLibraryReachableClasses = true, assumeLibraryMacroValues = true, assumeUnspelledFunctions = true;
  bool assumeIdempotentStandardHeaders = true, assumeUnobservedGuardMacros = true;
  std::vector<std::string> compile, keep, verifyRun;
};
Result<CliOptions> parseDistillOptions(const std::vector<std::string>&);
std::string distillHelp();
std::string distillVersion();
} // namespace distill
