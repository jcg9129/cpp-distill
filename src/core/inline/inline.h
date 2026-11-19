// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/command/command.h"
#include "core/names/names.h"
#include "core/probe/probe.h"
#include <compare>
#include <cstdint>

namespace distill {
struct IncludeDirectory { std::filesystem::path path; bool system = false, framework = false, compilerSystem = false; };
struct IncludeSearch {
  std::filesystem::path cwd;
  std::vector<IncludeDirectory> quote, angle;
};
Result<IncludeSearch> buildIncludeSearch(const CompileCommand&, const SearchLists&);
struct HeaderLookup {
  std::filesystem::path path; // Absolute filesystem access path, spelling retained.
  std::string fileName; // GCC's __FILE__ spelling, relative to command cwd if relative.
  bool found = false, system = false;
  std::optional<std::filesystem::path> systemDirectory;
  bool compilerSystem = false; // Resolved file is beneath a compiler-owned directory.
};
// Absolute includes are user files unless their resolved path lies under a system
// directory. A missing absolute angle name is kept; a missing quoted name errors.
Result<HeaderLookup> lookupHeader(const IncludeSearch&, std::string_view header,
                                bool angle, std::string_view includerFileName);
struct FileIdentity {
  std::uint64_t device = 0, file = 0;
  auto operator<=>(const FileIdentity&) const = default;
};
Result<FileIdentity> fileIdentity(const std::filesystem::path&);
std::string onceGuardName(FileIdentity, std::uint64_t salt = 0);
struct InlineOptions {
  std::optional<std::chrono::steady_clock::time_point> deadline;
  std::size_t maxIncludeDepth = 200, maxOutputBytes = 64 * 1024 * 1024;
  NameLists names;
  std::vector<std::string> libraries;
};
struct UserFile {
  std::filesystem::path path;
  std::string fileName;
  FileIdentity identity;
  std::size_t size = 0, times = 0; // Full renders; elided spellings retain zero.
};
struct SourceLocation { std::string file; std::size_t line = 0; auto operator<=>(const SourceLocation&) const = default; };
struct SystemInclude {
  SourceLocation location;
  std::string spelling; // Operand without delimiters.
  bool angle = false, rewritten = false, found = false, standard = false;
  std::optional<std::filesystem::path> directory;
  bool compilerSystem = false;
};
struct InlineFlags { bool counter = false, pushMacro = false, popMacro = false, pragmaOperator = false; };
struct InlineResult {
  std::set<std::string> assumptions;
  std::string text;
  std::vector<UserFile> userFiles; // One record per opened spelling; identity may repeat.
  std::vector<SystemInclude> systemIncludes; // One record per rendered textual occurrence.
  InlineFlags flags;
  std::vector<std::size_t> keepOffsets; // Physical declaration occurrences before stripping.
  std::vector<std::optional<SourceLocation>> lineMap; // nullopt = generated marker/guard.
};
Result<InlineResult> inlineSources(const CompileCommand&, const IncludeSearch&, const InlineOptions& = {});
} // namespace distill
