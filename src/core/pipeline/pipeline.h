// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/validate/configuration.h"

namespace distill {
struct DistillOptions {
  const Profile* profile = nullptr;
  bool compilerExplicit = true, strict = false;
  std::chrono::milliseconds stepTimeout{30000};
  std::size_t maxProcessOutput = 4 * 1024 * 1024;
  // Defaults to the OS temporary directory; tests may redirect it.
  std::optional<std::filesystem::path> temporaryRoot;
  std::function<void(std::string_view)> progress;
};
struct ArtifactCheck {
  std::string artifact, status;
  std::optional<Configuration> configuration;
  std::vector<ResolutionDecision> satisfied;
};
struct ProcessRecord { std::string step; ProcessSpec spec; };
struct PipelineResult {
  int exitCode = 1;
  std::string artifact, verification = "local", runtime = "not requested", bytes;
  std::string diagnostic, resultLine, report;
  std::optional<std::filesystem::path> keptWork;
  std::vector<ArtifactCheck> checks;
  ResolveResult resolution;
  std::set<std::string> assumptions;
  std::vector<ProcessRecord> processes;
};
// Does not publish: bytes are the exact validated bytes for the caller to write.
// Expected build/check failures return a result with exitCode=1 or raw fallback.
Result<PipelineResult> distillNoPrune(const CompileCommand&, const DistillOptions&,
                                    ConditionEvaluator&, ProcessRunner = runProcess);
} // namespace distill
