// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/command/options.h"
#include "core/path.h"
#include "distill_build_version.h"
#include <charconv>

namespace distill {
Result<CliOptions> parseDistillOptions(const std::vector<std::string>& args) {
  CliOptions o;
  bool speedOptionSpecified = false;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const auto& a = args[i];
    if (a == "--help") { o.help = true; return o; }
    if (a == "--version") { o.version = true; return o; }
    if (a == "--no-prune") o.noPrune = true;
    else if (a == "--merge-only") o.mergeOnly = true;
    else if (a == "--no-assume" || a.starts_with("--no-assume=")) {
      std::string names;
      if (a == "--no-assume") {
        if (++i == args.size()) return Error{"missing value for --no-assume; switchable names: instantiation-effects, library-spelled-members, library-macro-values, unspelled-functions, library-spelled-names, idempotent-standard-headers, unobserved-guard-macros"};
        names = args[i];
      } else names = a.substr(12);
      for (std::size_t begin = 0;;) {
        const auto end = names.find(',',begin);
        const auto name = names.substr(begin,end == names.npos ? end : end-begin);
        if (name != "instantiation-effects" && name != "library-spelled-members" && name != "library-macro-values" && name != "unspelled-functions" && name != "library-spelled-names" && name != "idempotent-standard-headers" && name != "unobserved-guard-macros")
          return Error{"unknown --no-assume name: " + name + "; switchable names: instantiation-effects, library-spelled-members, library-macro-values, unspelled-functions, library-spelled-names, idempotent-standard-headers, unobserved-guard-macros"};
        if (name == "instantiation-effects") o.assumeInstantiationEffects = false;
        else if (name == "library-spelled-members") o.assumeLibrarySpelledMembers = false;
        else if (name == "library-macro-values") o.assumeLibraryMacroValues = false;
        else if (name == "unspelled-functions") o.assumeUnspelledFunctions = false;
        else if (name == "library-spelled-names") o.assumeLibrarySpelledNames = false;
        else if (name == "idempotent-standard-headers") o.assumeIdempotentStandardHeaders = false;
        else o.assumeUnobservedGuardMacros = false;
        if (end == names.npos) break;
        begin = end+1;
      }
    }
    else if (a == "--jobs" || a == "-j" || a.starts_with("--jobs=")) {
      speedOptionSpecified = true;
      std::string value;
      if (a == "--jobs" || a == "-j") {
        if (++i == args.size()) return Error{"missing value for " + a};
        value = args[i];
      } else value = a.substr(7);
      const auto parsed = std::from_chars(value.data(), value.data() + value.size(), o.jobs);
      if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || o.jobs == 0)
        return Error{"--jobs requires a positive integer"};
    }
    else if (a == "--verify" || a.starts_with("--verify=")) {
      speedOptionSpecified = true;
      std::string value;
      if (a == "--verify") {
        if (++i == args.size()) return Error{"missing value for --verify"};
        value = args[i];
      } else value = a.substr(9);
      if (value != "all" && value != "final") return Error{"--verify requires all or final"};
      o.verifyFinal = value == "final";
    }
    else if (a == "--baseline" || a.starts_with("--baseline=")) {
      speedOptionSpecified = true;
      std::string value;
      if (a == "--baseline") {
        if (++i == args.size()) return Error{"missing value for --baseline"};
        value = args[i];
      } else value = a.substr(11);
      if (value != "build" && value != "skip") return Error{"--baseline requires build or skip"};
      o.skipBaseline = value == "skip";
    }
    else if (a == "--strict") o.strict = true;
    else if (a == "-v") o.verbose = true;
    else if (a == "--strip-comments") o.stripComments = true;
    else if (a == "--no-line-markers") o.noLineMarkers = true;
    else if (a == "--shorten-names") { o.shortenNames = true; o.stripComments = true; o.noLineMarkers = true; o.compactWhitespace = true; }
    else if (a == "--compact-whitespace") { o.compactWhitespace = true; o.noLineMarkers = true; }
    else if (a == "--keep" || a == "--verify-run") {
      if (++i == args.size() || args[i].empty() || args[i].starts_with('-')) return Error{"missing value for " + a};
      (a == "--keep" ? o.keep : o.verifyRun).push_back(args[i]);
    }
    else if (a == "--judge-probe" || a == "--judge-check" || a == "--part" || a == "--record") {
      if (++i == args.size() || args[i].empty() || args[i].starts_with('-')) return Error{"missing value for " + a};
      auto* target = a == "--judge-probe" ? &o.judgeProbe : a == "--judge-check" ? &o.judgeCheck : a == "--part" ? &o.part : &o.record;
      if (*target) return Error{"duplicate option: " + a};
      *target = args[i];
    }
    else if (a == "-o" || a == "--judge" || a == "--report") {
      if (++i == args.size() || args[i].empty()) return Error{"missing value for " + a};
      (a == "-o" ? o.output : a == "--judge" ? o.judge : o.report) = args[i];
    } else if (a.starts_with('-')) return Error{"unknown distill option: " + a + " (compiler options must follow the compiler name)"};
    else if (o.judgeCheck) o.judgeOutputs.push_back(a);
    else { o.compile.assign(args.begin() + i, args.end()); break; }
  }
  if (o.judgeProbe || o.judgeCheck) {
    if ((o.judgeProbe && o.judgeCheck) || o.judge || !o.compile.empty() || o.output || o.report ||
        speedOptionSpecified || o.noPrune || o.mergeOnly || !o.assumeLibrarySpelledNames || o.shortenNames || !o.assumeInstantiationEffects || !o.assumeLibrarySpelledMembers || !o.assumeLibraryMacroValues || !o.assumeUnspelledFunctions || !o.assumeIdempotentStandardHeaders || !o.assumeUnobservedGuardMacros || o.stripComments || o.noLineMarkers || o.strict || !o.keep.empty() || !o.verifyRun.empty())
      return Error{"judge diagnostics cannot be combined with distillation options or a compile command"};
    if (o.part && !o.judgeProbe) return Error{"--part requires --judge-probe"};
    if (o.record && !o.judgeCheck) return Error{"--record requires --judge-check"};
    if (o.judgeCheck && o.judgeOutputs.empty()) return Error{"--judge-check requires output files"};
    return o;
  }
  if (o.mergeOnly) {
    const char* conflict = nullptr;
    if (o.shortenNames) conflict = "--shorten-names";
    else if (o.compactWhitespace) conflict = "--compact-whitespace";
    else if (o.noLineMarkers) conflict = "--no-line-markers";
    else if (!o.keep.empty()) conflict = "--keep";
    else if (o.strict) conflict = "--strict";
    else if (!o.assumeInstantiationEffects || !o.assumeLibrarySpelledMembers || !o.assumeLibraryMacroValues ||
             !o.assumeUnspelledFunctions || !o.assumeLibrarySpelledNames ||
             !o.assumeIdempotentStandardHeaders || !o.assumeUnobservedGuardMacros) conflict = "--no-assume";
    if (conflict) return Error{std::string("--merge-only cannot be combined with ") + conflict};
  }
  if (o.skipBaseline && !o.verifyRun.empty()) return Error{"--baseline=skip cannot be combined with --verify-run"};
  if (o.part || o.record) return Error{"--part/--record require a judge diagnostic command"};
  if (o.compile.empty() && o.judge == "list") return o;
  if (o.compile.empty()) return Error{"missing compile command or source file"};
  const auto ext = pathToUtf8(pathFromUtf8(o.compile.front()).extension());
  if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c++" || ext == ".C") {
    o.compile.insert(o.compile.begin(), "g++"); o.compilerExplicit = false;
  }
  return o;
}
std::string distillHelp() {
  return "Usage: distill [distill options] [<compile command>]\n"
      "  -o <file>          output (default stdout)\n"
      "  --judge <selector>  profile name, alias or TOML path; list shows profiles\n"
      "  --judge-probe <selector> [--part N/M] print diagnostic source\n"
      "  --judge-check <selector> <output-file>... [--record <dir>] compare diagnostics\n"
      "  --report <file>    validation, conditional and pruning report\n"
      "  --no-prune         keep every declaration, but still resolve preprocessor conditionals\n"
      "                     for the judge and remove inactive #if branches (flat);\n"
      "                     use --merge-only to keep everything\n"
      "  --merge-only       inline local headers only (raw); keep every conditional and line of code\n"
      "                     baseline and raw checks still run; --judge, --verify-run,\n"
      "                     --strip-comments and redundant --no-prune are allowed\n"
      "                     cannot combine with --no-line-markers, --compact-whitespace,\n"
      "                     --shorten-names, --keep, --strict or --no-assume\n"
      "  --no-assume=<name> disable an assumption or policy (repeatable, comma lists accepted)\n"
      "                     switchable names: instantiation-effects (A11), library-spelled-members (A12), library-spelled-names (A15), library-macro-values (A9 standard macros), unspelled-functions (D3f), idempotent-standard-headers (policy), unobserved-guard-macros (A14)\n"
      "  --keep <name>      extra pruning root (repeatable indexed name)\n"
      "  --strip-comments   remove comments during checked rendering\n"
      "  --no-line-markers  omit #line markers from a pruned result when no user code\n"
      "                     can observe source positions (else kept; report says why)\n"
      "  --compact-whitespace trim line edges and empty lines when positions are unobserved;\n"
      "                     implies --no-line-markers (else unchanged; report says why)\n"
      "  --shorten-names    readably shorten identifiers in a checked pruned artifact;\n"
      "                     implies --strip-comments --no-line-markers --compact-whitespace\n"
      "  --jobs=N, -j N     concurrent independent compiler processes (default 1)\n"
      "  --baseline=build|skip build the original (default build); skip requires no samples\n"
      "                     and no assembly, modules, unmodelled flags or external library options\n"
      "  --verify=all|final check all renderings (default all), or only the last,\n"
      "                     checking earlier renderings on failure; raw/flat/pruned unchanged\n"
      "  --verify-run <path> compare runs on a file or sorted directory *.in (repeatable)\n"
      "  --strict           exit 3 unless pruned with target verification\n"
      "  -v                 progress on stderr\n"
      "  --help             show this help\n"
      "  --version          show version, commit and LLVM revision\n"
      "distill main.cpp means distill g++ main.cpp\n";
}
std::string distillVersion() {
  return "distill " DISTILL_BUILD_VERSION " (commit " DISTILL_BUILD_COMMIT
      "; LLVM " DISTILL_BUILD_LLVM_REVISION ")\n";
}
} // namespace distill
