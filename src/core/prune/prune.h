// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/validate/configuration.h"
#include "core/lex/scan.h"
#include <limits>

namespace distill {
using FamilyId = std::size_t;
inline constexpr FamilyId noFamily = std::numeric_limits<FamilyId>::max();
inline constexpr std::string_view pruningPolicyVersion = "D3e";
inline std::string_view assumptionLabel(std::string_view id) {
  if (id == "A11") return "A11 instantiation-effects";
  if (id == "A12") return "A12 library-spelled-members";
  if (id == "A14") return "A14 unobserved-guard-macros";
  if (id == "D3f") return "D3f unspelled-functions";
  return id;
}
struct EditRange {
  std::size_t begin = 0, end = 0;
  bool operator==(const EditRange&) const = default;
};
enum class FamilyKind { declaration, function, record, enumeration, alias, variable, conceptDecl,
                        specialization, usingDecl, assertion, instantiation };
struct PruneFamily {
  FamilyKind kind = FamilyKind::declaration;
  std::string label;
  // Namespace-scope indexed names. A member unit's own spelling is separate
  // in memberName, so effect indexing can use the whole-record boundary while
  // preserving any nonmember names discovered in its text.
  std::set<std::string, std::less<>> names, callableNames, dependentNames;
  std::vector<EditRange> ranges;
  // Written declarations represented by this row, independent of byte-range
  // partitioning (e.g. carving member spellings out of an enclosing function).
  std::size_t sourceDeclarationCount = 1;
  std::vector<FamilyId> edges, associatedTypes;
  std::optional<SourceLocation> original;
  std::string pinned, neverRemoved, root;
  // A template pattern whose instantiation can affect other families/runtime.
  // Reverse potential dependencies preserve its otherwise-unused triggers.
  std::string instantiationEffect;
  // Verified on every non-member callable in a record family: a parameter
  // directly carries that record family, without target-selected aliases.
  bool boundedSystemHooks = false;
  bool nonMemberOperator = false, namespaceUsing = false, externCDefinition = false;
  bool systemSpecialization = false;
  // Worker certificate for this declaration, intersected across family unions
  // by retention. False for non-function declarations and unclassified models.
  bool unspelledFunctionEligible = false;
  // D3e: same-name declarations share one unit; owner is the
  // retained record's structural family (also for records nested in it).
  FamilyId memberOf = noFamily;
  std::string memberName, memberBlocker;
};
struct PruneMacroExpansion {
  std::size_t offset = 0;
  std::string name;
  bool user = true, condition = false, predefined = false;
  MacroFact parse, judge;
  bool paste = false, externalPasteOperand = false, pasteOperandsKnown = true;
  // A macro whose definition has a #, #@ or ## operator (any provenance) expands
  // within the same physical user invocation span: the replacement's spelling
  // may reach a stringize/paste operand. Conservative; see the D3d-4 review.
  bool hashOperator = false;
};
// Integer literal widths of the Clang parse target, reported by the worker.
struct LiteralWidths {
  unsigned intBits = 0, longBits = 0, longLongBits = 0;
  bool operator==(const LiteralWidths&) const = default;
};
struct SystemEntity {
  std::string label;
  std::vector<FamilyId> userEdges;
  std::vector<std::size_t> systemEdges;
};
struct SystemUse { FamilyId family = noFamily; std::size_t entity = 0; };
// A construct in user code whose behaviour may depend on presumed source
// positions; any one keeps generated #line markers under --no-line-markers
// (docs/reviews/2026-11-18-no-line-markers.md). Offset: user expansion offset.
struct PositionUse {
  std::size_t offset = 0;
  std::string what;
  bool operator==(const PositionUse&) const = default;
};
struct KeepAnnotation { std::size_t offset = 0; FamilyId family = noFamily; };
struct PruningModel {
  // Part of the serialized worker input/result identity, not a parse-target fact.
  bool assumeInstantiationEffects = true;
  bool assumeLibrarySpelledMembers = true;
  bool assumeUnspelledFunctions = true;
  // Pruned-renderer settings, also immutable worker input/result identity.
  bool assumeIdempotentStandardHeaders = true, assumeUnobservedGuardMacros = true;
  // Raw system-header identifiers plus identifiers emitted through system macro
  // expansion in the full unpruned TU, unioned with all scanned-library names.
  bool libraryMemberNamesComputed = false;
  std::vector<std::string> memberDisabledReasons;
  std::set<std::string, std::less<>> libraryMemberNames;
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
  std::optional<LiteralWidths> parseWidths; // Set by the worker from Clang's TargetInfo.
  bool judgeProfile = false, judgeComplete = false;
  bool unknownInUserCode = false;
  std::vector<PositionUse> positionUses; // Set by the worker.
};
struct FamilyRetention {
  bool kept = false;
  std::string reason = "removed: outside the retention closure";
  FamilyId cause = noFamily, initialCause = noFamily;
};
// Unique deleted input physical lines/bytes, including directive trivia;
// generated marker bytes and declaration ownership are accounted separately.
struct TrimmingStats {
  std::size_t includeLines = 0, includeBytes = 0, guardLines = 0, guardBytes = 0;
};
struct PruningResult {
  TrimmingStats trimming;
  std::vector<FamilyRetention> families;
  std::vector<std::string> disabledReasons, memberDisabledReasons;
  std::string policy = std::string(pruningPolicyVersion), registryVersion, registryDigest, dialect;
  std::set<std::string> assumptions;
  std::map<std::string, std::size_t> primaryBytes, transitiveBytes;
  std::size_t keptBytes = 0, removedBytes = 0;
  std::size_t rule3UnkeptDeclarations = 0, rule3UnkeptBytes = 0;
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
