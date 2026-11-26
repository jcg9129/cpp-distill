// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/prune/extract.h"

namespace distill {
struct RunSample { std::string name; std::filesystem::path input; };
Result<std::vector<RunSample>> collectRunSamples(const std::vector<std::string>& paths,
                                                const std::filesystem::path& cwd);
struct DistillOptions {
  bool noPrune = false, mergeOnly = false, stripComments = false;
  bool assumeLibrarySpelledNames = true;
  // Implies comment stripping, marker omission and checked compaction.
  bool shortenNames = false;
  // Independent compiler invocations only; custom ProcessRunners must be safe
  // to call concurrently when jobs > 1. Reports retain serial order.
  unsigned jobs = 1;
  // The baseline executable is used only for sample comparisons. Raw still
  // passes its preprocessing and full build checks. Rejected with samples,
  // assembly, modules, unmodelled flags or external library options.
  bool skipBaseline = false;
  // Defer intermediate rendering builds/runs, retaining every invariant and
  // all raw/flat/pruned checks. Check deferred renderings in reverse on failure.
  bool verifyFinal = false;
  bool assumeInstantiationEffects = true, assumeLibrarySpelledMembers = true, assumeLibraryMacroValues = true, assumeUnspelledFunctions = true;
  bool assumeIdempotentStandardHeaders = true, assumeUnobservedGuardMacros = true;
  // Opt-in: omit generated #line markers from a pruned result when the parse
  // proves no position is observed (docs/reviews/2026-11-18-no-line-markers.md).
  bool noLineMarkers = false;
  // Implies noLineMarkers; compact only the checked marker-free artifact.
  bool compactWhitespace = false;
  std::vector<std::string> keep;
  std::filesystem::path pruningWorker;
  // CLI selection and internal callers share the same bounded comparisons.
  std::vector<RunSample> samples;
  const Profile* profile = nullptr;
  bool compilerExplicit = true, strict = false;
  std::chrono::milliseconds stepTimeout{30000};
  std::size_t maxProcessOutput = 4 * 1024 * 1024;
  // -E can expand standard headers far beyond the inline source. Per stream;
  // separate from probes/builds. Same bound as the §3 inline-size limit.
  std::size_t maxPreprocessOutput = 64 * 1024 * 1024;
  // Defaults to the OS temporary directory; tests may redirect it.
  std::optional<std::filesystem::path> temporaryRoot;
  std::function<void(std::string_view)> progress;
};
struct ArtifactCheck {
  std::string artifact, status;
  std::optional<Configuration> configuration;
  std::vector<ResolutionDecision> satisfied;
};
struct ProcessRecord { std::string step; ProcessSpec spec; };
struct PipelineResult {
  int exitCode = 1;
  std::string artifact, verification = "local", runtime = "not requested", bytes;
  std::string diagnostic, resultLine, report;
  std::vector<std::string> optionLines;
  // With noLineMarkers: "omitted" or "kept: <first reason>"; empty otherwise.
  std::string lineMarkers;
  // With compactWhitespace: "compacted" or "kept: <first reason>".
  std::string compactWhitespace;
  // "renamed=N spellings saved=B bytes" or "kept: <why>".
  std::string shortenNames;
  std::optional<std::filesystem::path> keptWork;
  std::vector<ArtifactCheck> checks;
  ResolveResult resolution;
  std::optional<PruningModel> pruningModel;
  std::optional<PruningResult> retention;
  std::set<std::string> assumptions;
  std::vector<ProcessRecord> processes;
};
// Does not publish: bytes are the exact validated bytes for the caller to write.
// Expected check failures return exitCode=1 or the last checked fallback.
Result<PipelineResult> distillProgram(const CompileCommand&, const DistillOptions&,
                                    ConditionEvaluator&, ProcessRunner = runProcess);
// Compatibility entry point for callers requesting inline/resolve only.
Result<PipelineResult> distillNoPrune(const CompileCommand&, const DistillOptions&,
                                    ConditionEvaluator&, ProcessRunner = runProcess);
} // namespace distill
