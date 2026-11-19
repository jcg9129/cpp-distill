// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/inline/inline.h"
#include "core/profile/profile.h"
#include "core/library/library.h"

namespace distill {
// PP arithmetic uses intmax_t/uintmax_t; the other widths affect literal parsing.
struct IntegerModel {
  unsigned charBits = 8, intBits = 32, longBits = 64, longLongBits = 64;
  unsigned wcharBits = 32, intmaxBits = 64;
  bool charSigned = true, wcharSigned = true;
  bool operator==(const IntegerModel&) const = default;
};
struct EvaluationRequirements {
  std::optional<std::string> dialect;
  std::optional<IntegerModel> integers;
  bool operator==(const EvaluationRequirements&) const = default;
};
// The source contains only closure definitions and #if, with unique result tokens.
// Implementations must suppress ALL implicit macro definitions and header search.
// Process-backed implementations honor deadline; an in-process engine must be
// contained by its caller (the CLI uses a bounded Clang worker).
struct ConditionInput {
  std::string source;
  EvaluationRequirements requirements;
  std::string trueToken = "DISTILL_CONDITION_TRUE", falseToken = "DISTILL_CONDITION_FALSE";
  std::optional<std::chrono::steady_clock::time_point> deadline = {};
};
// Undefine intrinsic/standard bindings that -undef may leave active.
std::string isolatedConditionSource(const ConditionInput&);
class ConditionEvaluator {
public:
  virtual ~ConditionEvaluator() = default;
  virtual Result<bool> evaluate(const ConditionInput&) = 0;
};
EvaluationRequirements profileRequirements(const Profile&);
// Supported fixed triples, independent of host architecture. No target backend needed.
Result<std::string> evaluationTriple(const IntegerModel&);
struct ExternalUse { std::string name; MacroFact fact; bool definitionUsed = false; };
enum class Decision { trueValue, falseValue, unknown, dead };
struct ResolutionDecision {
  std::size_t rawLine = 0;
  std::optional<SourceLocation> original;
  std::string directive;
  Decision decision = Decision::dead;
  std::string reason;
  std::vector<ExternalUse> external;
  EvaluationRequirements requirements;
};
struct ResolveOptions {
  std::optional<std::chrono::steady_clock::time_point> deadline = {};
  const Profile* profile = nullptr;
  EvaluationRequirements requirements; // Missing fields inherit from profile.
  NameLists names;
  const LibraryData* libraryData = &embeddedLibraryData();
  // Local satisfaction reruns have compiler observations, not a reproduced
  // judge library build. They may use only the major's audited agreement.
  bool libraryAgreementOnly = false;
  std::size_t maxConditionTokens = 100000, maxNesting = 200;
  std::size_t maxOutputBytes = 128 * 1024 * 1024;
  // Raw-text lines of conditional directives kept unknown (cross-host demotion).
  // A forced #else/#endif forces its block; everything nested in a forced
  // block stays unknown too, so the block is byte-identical to raw.
  std::set<std::size_t> forcedUnknown;
};
struct ResolveResult {
  std::vector<std::size_t> keepOffsets;
  std::string text;
  std::vector<std::optional<SourceLocation>> lineMap;
  std::vector<ResolutionDecision> decisions;
  std::vector<std::string> disabledReasons;
  bool unknownInUserCode = false;
  std::set<std::string> libraryProvenance; // Only facts used by resolved decisions.
  std::string libraryDisabledReason; // Coverage rejection; other facts still apply.
  std::map<std::string, std::size_t, std::less<>> counts;
};
Result<ResolveResult> resolveConditions(const InlineResult& raw, const CompileCommand&,
                                      ConditionEvaluator&, const ResolveOptions& = {});
// Directional: b satisfies every true/false decision made by a, or proves
// its directive unreachable. Unknown/dead entries in a impose no requirement.
// Locations/directive inventory must match.
bool sameDecisions(const ResolveResult& a, const ResolveResult& b);
// Indices of a's true/false decisions that b neither reproduces nor proves dead.
// Empty when the inventories differ or either resolution is disabled.
std::vector<std::size_t> disagreements(const ResolveResult& a, const ResolveResult& b);
// Re-resolves with options plus every directive `observed` contradicts forced
// unknown, repeating until `observed` satisfies the result. Fails if that
// cannot be reached (disabled, inventory differs, no progress, deadline).
// `demoted` receives the decision entries that were forced, as they were before.
Result<ResolveResult> demoteDisagreements(const InlineResult&, const CompileCommand&, ConditionEvaluator&,
                                          ResolveOptions, ResolveResult resolution, const ResolveResult& observed,
                                          std::vector<ResolutionDecision>* demoted = nullptr);
} // namespace distill
