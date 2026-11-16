// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/validate/configuration.h"
namespace distill::testing {
// A runtime fixture of THIS compiler. Never a claim about a shipped judge.
Result<Profile> captureProfile(const std::filesystem::path& compiler,
                              const std::filesystem::path& directory,
                              ProcessRunner runner = runProcess);
} // namespace distill::testing
