// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/prune/prune.h"
namespace distill {
#ifdef _WIN32
inline constexpr std::string_view pruningResourceDirectory = "C:/__cpp_distill_resource__";
#else
inline constexpr std::string_view pruningResourceDirectory = "/__cpp_distill_resource__";
#endif
// The selected, satisfying flat configuration is required; no local reprobe.
Result<PruningModel> preparePruning(const Configuration&, const InlineResult&, const ResolveResult&,
                                  const Profile* judge = nullptr,
                                  const std::vector<std::string>& keep = {});
struct PruneWorkerOptions {
  std::filesystem::path executable, temporaryRoot;
  std::chrono::milliseconds timeout{30000};
  std::optional<std::chrono::steady_clock::time_point> deadline;
  std::size_t maxOutputBytes = 64 * 1024 * 1024;
  ProcessRunner runner = runProcess;
};
// Uses the same D2 process-group/Job Object containment as the condition worker.
// Worker errors/limits disable pruning and retain every input byte.
Result<PruningModel> extractPruning(const PruningModel&, const PruneWorkerOptions&);
} // namespace distill
