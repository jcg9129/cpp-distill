// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/probe/probe.h"
#include <functional>
#include <set>

namespace distill {
struct ProfileProvenance {
  std::filesystem::path file;
  std::string updated;
  std::vector<std::string> sources;
};
enum class MacroState { undefined, known, definedUnknown, unknown };
enum class FactOrigin { compilerBuiltin, startupHeader, commandOperation, specialBuiltin,
                        nativeModel, a5, a4, allowList };
struct MacroFact {
  MacroState state = MacroState::unknown;
  std::optional<Macro> definition;
  std::vector<FactOrigin> provenance;
  bool activeSpecialBuiltin() const;
};
using MacroFacts = std::map<std::string, MacroFact, std::less<>>;
struct SnapshotMetadata {
  std::string compilerPath, versionOutput, gccVersion, dumpmachine, build, snapshotCompiler;
  bool reproduced = false;
};
struct Profile {
  std::string name, description, triple, compilerVersion;
  CompilerIdentity compiler;
  bool patchSpecified = false;
  Dialect dialect;
  std::vector<std::string> flags, link, inert, libraries;
  std::vector<LocalMacro> macroOperations;
  std::optional<std::pair<std::string, std::string>> build, snapshotCompiler;
  MacroTable judgeMacros;
  MacroTable predefined;
  bool reproduced = false;
  SnapshotMetadata snapshotMetadata;
  MacroFacts snapshotFacts;
  MacroFact initialState(std::string_view name) const;
  std::optional<std::string> snapshot;

  std::vector<std::string> check;
  ProfileProvenance provenance;
  // An error names every collision with A3; callers must disable resolution.
  Result<bool> checkA3(const std::set<std::string, std::less<>>& userDefines) const;
};
// Registry/model data are embedded, never read from the profile directory.
std::set<std::string, std::less<>> specialBuiltins(const CompilerIdentity& compiler);
std::map<std::string, std::string, std::less<>> nativeDependencies(const CompilerIdentity& compiler);
bool approximateAllowed(std::string_view name);
bool compatibleProfileTriple(std::string_view expected, std::string_view actual);
Result<bool> parseProfileSnapshot(Profile& profile, std::string_view text);
using SnapshotReader = std::function<Result<std::string>(const std::filesystem::path&)>;
Result<Profile> parseProfile(std::string_view text, const std::filesystem::path& file,
                             SnapshotReader reader = {});
Result<Profile> loadProfile(const std::filesystem::path& file);
using ProfileFiles = std::map<std::string, std::string, std::less<>>;
const ProfileFiles& embeddedProfileFiles();
struct ProfileLookup {
  // No process environment or home directory is read implicitly. The CLI supplies
  // the environment. An explicit directory overrides the environment-derived one.
  std::map<std::string, std::string, std::less<>> environment;
  std::optional<std::filesystem::path> userDirectory;
#ifdef _WIN32
  bool windows = true;
#else
  bool windows = false;
#endif
};
std::optional<std::filesystem::path> profileUserDirectory(const ProfileLookup& lookup);
Result<Profile> lookupProfile(std::string_view nameOrFile, const ProfileLookup& lookup = {},
                              const ProfileFiles& embedded = embeddedProfileFiles());
} // namespace distill
