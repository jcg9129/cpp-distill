// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/result.h"
#include <optional>
#include <string>
#include <vector>

namespace distill {
struct CliOptions {
  bool help = false, strict = false, verbose = false, compilerExplicit = true;
  std::optional<std::string> output, judge, report;
  std::vector<std::string> compile;
};
Result<CliOptions> parseDistillOptions(const std::vector<std::string>&);
std::string distillHelp();
} // namespace distill
