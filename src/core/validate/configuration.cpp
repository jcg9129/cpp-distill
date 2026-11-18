// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/validate/configuration.h"
#include "core/path.h"
#include <algorithm>

namespace distill {
bool hostDependent(const CompileCommand& c) {
  return std::any_of(c.args.begin(), c.args.end(), [](const auto& a) {
    return (a.option == "-march=" || a.option == "-mtune=" || a.option == "-mcpu=") && a.value == "native";
  });
}
Result<Configuration> probeConfiguration(CompileCommand c, std::string kind, const ProbeOptions& options) {
  auto probe = probeCompiler(c.compiler, c, options);
  if (!probe.ok()) return probe.error();
  Configuration result;
  result.kind = std::move(kind); result.command = std::move(c); result.probe = std::move(probe.value());
  auto& p = result.facts;
  p.name = "configuration"; p.compiler = result.probe.identity; p.triple = result.probe.triple;
  p.dialect = result.probe.dialect;
  // A1/A2 library policy for the supported target families, also needed by
  // satisfaction reruns. Unknown targets retain the conservative intersection.
  if (p.triple.find("mingw") != p.triple.npos || p.triple.find("cygwin") != p.triple.npos)
    p.libraries = {"mingw-w64", "libstdc++"};
  else if (p.triple.find("linux") != p.triple.npos)
    p.libraries = {"glibc", "libstdc++"};
  else if (p.triple.find("darwin") != p.triple.npos)
    p.libraries = {"macos-libc", "libstdc++"};  // Homebrew GCC: libstdc++ headers too.
  // Complete registered local GCCs supply exhaustive observations. Unsupported
  // releases remain approximate, just like an incomplete snapshot registry.
  p.reproduced = !specialBuiltins(p.compiler).empty();
  p.snapshot = "observed";
  p.predefined = result.probe.macros;
  p.macroOperations = result.command.localMacros();
  for (const auto& [name, macro] : result.probe.macros)
    p.snapshotFacts[name] = {MacroState::known, macro, {FactOrigin::compilerBuiltin}};
  for (const auto& [name, macro] : result.probe.startupMacros)
    p.snapshotFacts[name] = {MacroState::known, macro, {FactOrigin::startupHeader}};
  for (const auto& [name, defined] : result.probe.special) if (!p.snapshotFacts.contains(name))
    p.snapshotFacts[name] = {defined ? MacroState::definedUnknown : MacroState::undefined, {}, {FactOrigin::specialBuiltin}};
  for (const auto& op : p.macroOperations) {
    const auto name = op.value.substr(0, op.value.find_first_of("(="));
    if (result.probe.startupMacros.contains(name)) continue;
    if (op.option == "-U") p.snapshotFacts[name] = {MacroState::undefined, {}, {FactOrigin::commandOperation}};
    else if (result.probe.macros.contains(name)) {
      p.judgeMacros[name] = result.probe.macros.at(name);
      p.snapshotFacts[name] = {MacroState::known, p.judgeMacros[name], {FactOrigin::commandOperation}};
    }
  }
  result.argv.push_back(pathToUtf8(result.command.compiler));
  for (const auto& a : result.command.args) if (a.classification != Class::input && a.classification != Class::controlled)
    result.argv.insert(result.argv.end(), a.spelling.begin(), a.spelling.end());
  return result;
}
Result<CompileCommand> flatCommand(const CompileCommand& local, const Profile* p,
                                  const std::filesystem::path& compiler, bool check) {
  std::vector<std::string> argv{pathToUtf8(compiler)};
  for (const auto& a : local.args) {
    if (a.classification == Class::input) { argv.push_back(pathToUtf8(local.source)); continue; }
    if (a.classification == Class::controlled || a.option == "-I" || a.option == "-iquote") continue;
    if (p && (a.option == "-D" || a.option == "-U" || a.option == "-std=")) continue;
    if (p && check && (a.classification == Class::libraries || a.option == "-l" || a.option == "-static" || a.option == "-static-libgcc" || a.option == "-static-libstdc++" || a.option == "-s")) continue;
    for (const auto& word : a.spelling) argv.push_back(word);
  }
  const auto profileBegin = argv.size();
  if (p) {
    argv.push_back("-std=" + p->dialect.name);
    argv.insert(argv.end(), p->flags.begin(), p->flags.end());
  }
  auto result = parseCompileCommand(argv, local.cwd);
  if (result.ok() && p) {
    std::size_t word = 1;
    for (auto& a : result.value().args) {
      // Count argv words, not arguments: local paired options must not shift
      // the profile boundary or exempt a same-spelled local occurrence.
      if (word >= profileBegin && a.classification == Class::unmodelled &&
          a.spelling.size() == 1 && std::find(p->inert.begin(), p->inert.end(), a.spelling.front()) != p->inert.end())
        a.profilePreprocessingInert = true;
      word += a.spelling.size();
    }
  }
  // Link options were already validated by the profile loader, separately from
  // compile options (which intentionally reject -Wl). Preserve their exact argv.
  if (result.ok() && p && check) for (const auto& flag : p->link)
    result.value().args.push_back({{flag}, Class::libraries, flag, {}});
  return result;
}
Result<bool> satisfies(const Configuration& c, const InlineResult& raw, const CompileCommand& command,
                       const ResolveResult& resolution, ConditionEvaluator& evaluator,
                       std::optional<std::chrono::steady_clock::time_point> deadline, ResolveResult* observed) {
  ResolveOptions options; options.profile = &c.facts; options.deadline = deadline;
  auto rerun = resolveConditions(raw, command, evaluator, options);
  if (!rerun.ok()) return rerun.error();
  const bool equal = sameDecisions(resolution, rerun.value());
  if (observed) *observed = std::move(rerun.value());
  return equal;
}
bool targetEquivalent(const Configuration& c, const Profile* p, bool hostCommand) {
  if (!p || !p->reproduced || hostCommand || hostDependent(c.command) ||
      c.probe.identity.family != p->compiler.family || c.probe.identity.major != p->compiler.major ||
      c.probe.identity.minor != p->compiler.minor || c.probe.identity.patch != p->compiler.patch ||
      !compatibleProfileTriple(p->triple, c.probe.triple) || c.probe.dialect.name != p->dialect.name) return false;
  // Equality is exhaustive, not just agreement on facts used in conditions.
  if (c.probe.macros != p->predefined) return false;
  for (const auto& name : specialBuiltins(p->compiler)) {
    auto observed = c.probe.special.find(name);
    auto saved = p->snapshotFacts.find(name);
    if (observed == c.probe.special.end() || saved == p->snapshotFacts.end()) return false;
    const bool defined = saved->second.state == MacroState::known || saved->second.state == MacroState::definedUnknown;
    if (observed->second != defined) return false;
  }
  // Caller configurations include full flags. Raw's local command must itself
  // contain them in order; coincidentally equal dumps cannot replace this check.
  std::size_t index = 1;
  for (const auto& flag : p->flags) {
    while (index < c.argv.size() && c.argv[index] != flag) ++index;
    if (index == c.argv.size()) return false;
    ++index;
  }
  return true;
}
} // namespace distill
