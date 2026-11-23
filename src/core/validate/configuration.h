// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/resolve/resolve.h"

namespace distill {
struct OptionChange { std::vector<std::string> spelling; std::string reason; };
struct Configuration {
  std::string kind; // local or check
  CompileCommand command;
  ProbeResult probe;
  Profile facts; // Local observed initial state, not a judge reproduction claim.
  std::vector<std::string> argv; // Compiler and exact ordered configuration options.
  std::vector<OptionChange> changes;
};
Result<Configuration> probeConfiguration(CompileCommand, std::string kind,
                                       const ProbeOptions& = {});
// check=true uses the profile link options; fallback retains local linker options.
Result<CompileCommand> flatCommand(const CompileCommand&, const Profile*,
                                  const std::filesystem::path& compiler, bool check);
Result<bool> satisfies(const Configuration&, const InlineResult&, const CompileCommand&,
                       const ResolveResult&, ConditionEvaluator&,
                       std::optional<std::chrono::steady_clock::time_point> deadline = {},
                       ResolveResult* observed = nullptr, bool assumeLibraryMacroValues = true);
bool targetEquivalent(const Configuration&, const Profile*, bool hostDependentCommand);
bool hostDependent(const CompileCommand&);
} // namespace distill
