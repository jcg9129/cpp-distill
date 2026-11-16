// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/validate/configuration.h"
#include "core/lex/scan.h"
#include <limits>

namespace distill {
using FamilyId = std::size_t;
inline constexpr FamilyId noFamily = std::numeric_limits<FamilyId>::max();
inline constexpr std::string_view pruningPolicyVersion = "D3b-1";
struct EditRange {
  std::size_t begin = 0, end = 0;
  bool operator==(const EditRange&) const = default;
};
enum class FamilyKind { declaration, function, record, enumeration, alias, variable, conceptDecl,
                        specialization, usingDecl, assertion, instantiation };
struct PruneFamily {
  FamilyKind kind = FamilyKind::declaration;
  std::string label;
  std::set<std::string, std::less<>> names, callableNames, dependentNames;
  std::vector<EditRange> ranges;
  std::vector<FamilyId> edges, associatedTypes;
  std::optional<SourceLocation> original;
  std::string pinned, neverRemoved, root;
  bool nonMemberOperator = false, namespaceUsing = false, externCDefinition = false;
  bool systemSpecialization = false;
};
struct PruneMacroExpansion {
  std::size_t offset = 0;
  std::string name;
  bool user = true, condition = false, predefined = false;
  MacroFact parse, judge;
  bool paste = false, externalPasteOperand = false, pasteOperandsKnown = true;
};
struct SystemEntity {
  std::string label;
  std::vector<FamilyId> userEdges;
  std::vector<std::size_t> systemEdges;
};
struct SystemUse { FamilyId family = noFamily; std::size_t entity = 0; };
struct KeepAnnotation { std::size_t offset = 0; FamilyId family = noFamily; };
struct PruningModel {
  std::string text, dialect;
  std::vector<PruneFamily> families;
  std::vector<std::pair<FamilyId, FamilyId>> unions;
  std::vector<EditRange> generatedMarkers, sourceDirectives;
  std::vector<std::optional<SourceLocation>> lineMap;
  std::vector<KeepAnnotation> annotations;
  std::vector<std::size_t> keepOffsets; // Pending physical declaration-token offsets, captured before stripping.
  std::set<std::string, std::less<>> keepNames;
  std::vector<PruneMacroExpansion> expansions;
  std::vector<SystemEntity> systemEntities;
  std::vector<SystemUse> systemUses;
  std::vector<std::string> includes, disabledReasons, untranslatedOptions, parseArgv;
  MacroFacts judgeInitial; // Effective initial judge facts, not checking-compiler facts.
  bool judgeProfile = false, judgeComplete = false;
  bool unknownInUserCode = false;
};
struct FamilyRetention {
  bool kept = false;
  std::string reason = "removed: outside the retention closure";
  FamilyId cause = noFamily, initialCause = noFamily;
};
struct PruningResult {
  std::vector<FamilyRetention> families;
  std::vector<std::string> disabledReasons;
  std::string policy = std::string(pruningPolicyVersion), registryVersion, registryDigest, dialect;
  std::set<std::string> assumptions;
  std::map<std::string, std::size_t> primaryBytes, transitiveBytes;
  std::size_t keptBytes = 0, removedBytes = 0;
};
Result<PruningResult> computeRetention(const PruningModel&);
Result<bool> staticNameInvariant(const PruningModel&, const PruningResult&, std::string_view rendered);
// Private bounded worker protocol. Parsing is strict and validates all indices/ranges.
Result<std::string> serializePruningModel(const PruningModel&);
Result<PruningModel> parsePruningModel(std::string_view);
// invalidField optionally identifies the failing protocol field path.
Result<bool> validatePruningModel(const PruningModel&, std::string* invalidField = nullptr);
// Identifier tokens plus literal-operator suffixes, excluding comments/strings.
Result<std::vector<Token>> pruningIdentifiers(std::string_view);
Result<std::vector<std::size_t>> captureKeepOffsets(std::string_view);
} // namespace distill
