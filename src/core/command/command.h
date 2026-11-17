// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/result.h"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace distill {
enum class Class { input, modelled, harmless, libraries, controlled, unmodelled };
struct Arg {
  std::vector<std::string> spelling;
  Class classification;
  std::string option;
  std::string value;
  // Set only for an exact inert occurrence appended from the selected profile.
  // This exempts preprocessing permission, never semantic parse translation.
  bool profilePreprocessingInert = false;
};
enum class IncludeKind { user, quote, system };
struct IncludePath { std::filesystem::path path; IncludeKind kind; };
struct LocalMacro { std::string option, value; };
struct Permission {
  bool allowed;
  std::vector<std::string> reasons;
  explicit operator bool() const noexcept { return allowed; }
};
struct CompileCommand {
  std::filesystem::path compiler, source, cwd;
  std::vector<Arg> args;
  std::vector<IncludePath> userIncludePaths() const;
  std::vector<IncludePath> systemIncludePaths() const;
  std::vector<LocalMacro> localMacros() const;
  std::optional<std::string> dialect() const;
  std::vector<Arg> flags(Class classification) const;
  Permission pruningAllowed() const;
  Permission resolutionAllowed() const;
  // generatedFile=false retains -I/-iquote for the baseline build.
  std::vector<std::string> buildArgv(const std::filesystem::path& source,
      const std::filesystem::path& output, bool keepLocalMacros,
      const std::vector<std::string>& extraDefines, bool generatedFile = true) const;
};
Result<CompileCommand> parseCompileCommand(std::vector<std::string> argv,
                                         std::filesystem::path cwd);
} // namespace distill
