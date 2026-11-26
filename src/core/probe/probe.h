// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/command/command.h"
#include "core/process/process.h"
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
  bool operator==(const Macro&) const = default;
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
  // Defaults to std::filesystem::temp_directory_path().
  std::optional<std::filesystem::path> temporaryRoot;
  ProcessRunner runner = runProcess;
  std::chrono::milliseconds timeout{30000};
  std::size_t maxOutputBytes = 4 * 1024 * 1024;
  // Optional prefetch of independent initial probes. Results retain input
  // order; consumption is separate so parse failures preserve serial traces.
  ProcessBatchRunner batchRunner = {};
  std::function<void(const ProcessSpec&)> batchConsumed = {};
};
struct ProbeResult {
  SearchLists searchLists;
  std::string triple;
  MacroTable macros;
  CompilerIdentity identity;
  Dialect dialect;
  std::map<std::string, bool, std::less<>> special;
  MacroTable startupMacros;
};
Result<SearchLists> parseSearchLists(std::string_view stderrOfDashV);
bool isGccInternalIncludeDir(const std::filesystem::path& path);
Result<MacroTable> parseMacros(std::string_view dashDM);
CompilerIdentity identifyCompiler(const MacroTable& macros);
Dialect effectiveDialect(std::optional<std::string> stdFlag, const MacroTable& macros);
Result<ProbeResult> probeCompiler(const std::filesystem::path& compiler,
                                const CompileCommand& command, ProbeOptions options = {});
} // namespace distill
