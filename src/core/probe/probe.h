// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/command/command.h"
#include <map>
#include <string_view>

namespace distill {
struct SearchDirectory {
  std::filesystem::path path;
  IncludeKind kind;
  bool framework = false;
  bool gccInternal = false;
};
struct SearchLists { std::vector<SearchDirectory> directories; };
struct Macro {
  bool functionLike = false;
  std::vector<std::string> parameters;
  bool variadic = false;
  std::string replacement;
};
using MacroTable = std::map<std::string, Macro, std::less<>>;
enum class CompilerFamily { gcc, clang, appleClang, unknown };
struct CompilerIdentity {
  CompilerFamily family = CompilerFamily::unknown;
  int major = 0, minor = 0, patch = 0;
};
struct Dialect { std::string name; bool gnu; long cplusplus; };
struct ProbeOptions {
  bool keepLocalMacros = true;
  std::vector<std::string> defines;
  // Defaults to the command cwd; keeps probe files inside the caller's workspace.
  std::optional<std::filesystem::path> temporaryRoot;
};
struct ProbeResult {
  SearchLists searchLists;
  std::string triple;
  MacroTable macros;
  CompilerIdentity identity;
  Dialect dialect;
};
Result<SearchLists> parseSearchLists(std::string_view stderrOfDashV);
bool isGccInternalIncludeDir(const std::filesystem::path& path);
Result<MacroTable> parseMacros(std::string_view dashDM);
CompilerIdentity identifyCompiler(const MacroTable& macros);
Dialect effectiveDialect(std::optional<std::string> stdFlag, const MacroTable& macros);
Result<ProbeResult> probeCompiler(const std::filesystem::path& compiler,
                                const CompileCommand& command, ProbeOptions options = {});
} // namespace distill
