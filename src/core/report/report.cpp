// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/report/report.h"
#include "core/report/prune.h"
#include "core/path.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace distill {
namespace {
const char* className(Class c) {
  switch (c) {
    case Class::input: return "input";
    case Class::modelled: return "modelled";
    case Class::harmless: return "harmless";
    case Class::libraries: return "libraries";
    case Class::controlled: return "controlled";
    case Class::unmodelled: return "unmodelled";
  }
  return "unknown";
}
std::string origins(const MacroFact& f, bool profile) {
  std::string s;
  for (auto o : f.provenance) {
    const char* name = "profile";
    switch (o) {
      case FactOrigin::a3: name = "A3"; break;
      case FactOrigin::a4: name = "A4"; break;
      case FactOrigin::a5: name = "A5"; break;
      case FactOrigin::source: name = "source"; break;
      case FactOrigin::noProfile: name = "no profile"; break;
      case FactOrigin::systemHeader: name = "system header"; break;
      case FactOrigin::pragma: name = "pragma"; break;
      case FactOrigin::join: name = "join"; break;
      case FactOrigin::commandOperation: name = profile ? "profile command" : "configuration command"; break;
      default: name = profile ? "profile" : "configuration"; break;
    }
    if (s.find(name) == s.npos) { if (!s.empty()) s += ", "; s += name; }
  }
  return s;
}
void facts(std::ostream& out, const std::vector<ResolutionDecision>& decisions, bool profile) {
  std::set<std::string> seen;
  for (const auto& d : decisions) if (d.decision == Decision::trueValue || d.decision == Decision::falseValue) {
    for (const auto& u : d.external) {
      std::string value = u.fact.state == MacroState::undefined ? "undefined" : u.fact.state == MacroState::known ? "known" : u.fact.state == MacroState::definedUnknown ? "defined, definition unknown" : "unknown";
      auto text = u.name + "=" + value;
      if (u.definitionUsed && u.fact.definition) text += " " + u.fact.definition->replacement;
      text += " (" + origins(u.fact, profile) + ")";
      if (seen.insert(text).second) out << "  fact " << text << '\n';
    }
    if (d.requirements.dialect && seen.insert("dialect").second) out << "  fact dialect=" << *d.requirements.dialect << '\n';
    if (d.requirements.integers && seen.insert("integers").second) {
      const auto& m = *d.requirements.integers;
      out << "  fact integer widths=" << m.charBits << '/' << m.intBits << '/' << m.longBits << '/' << m.longLongBits << '/' << m.wcharBits << '/' << m.intmaxBits
          << ", char signed=" << m.charSigned << ", wchar signed=" << m.wcharSigned << '\n';
    }
  }
}
}
std::string renderReport(const PipelineResult& r, const CompileCommand& command, const Profile* profile) {
  std::ostringstream out;
  out << r.resultLine << '\n';
  out << "profile: " << (profile ? profile->name + " updated " + profile->provenance.updated : "none") << '\n';
  if (profile) {
    out << profileSelectionLine(*profile)
        << "judge selector: " << std::quoted(profile->provenance.selector) << '\n'
        << "judge resolved name: " << profile->name << '\n'
        << "judge origin: " << profile->provenance.origin;
    if (profile->provenance.origin == "user") out << ' ' << std::quoted(pathToUtf8(profile->provenance.file));
    out << "\njudge digest: sha256:" << profile->provenance.contentDigest << '\n';
    for (const auto& warning : profile->warnings) out << warning << '\n';
    if (!profile->inert.empty()) out << "profile inert: preprocessing-only guarantee; untranslated semantic options still disable pruning\n";
  } else out << "judge facts: unknown by design without --judge; target-dependent conditionals need a profile for pruning\n";
  out << "assumptions:";
  if (r.assumptions.empty()) out << " none";
  for (const auto& a : r.assumptions) out << ' ' << a;
  out << '\n';
  if (r.pruningModel && r.retention) {
    auto pruning = renderPruningReport(*r.pruningModel,*r.retention);
    if (pruning.ok()) out << pruning.value();
    else out << "pruning report error: " << pruning.error().message << '\n';
  }
  for (const auto& a : command.args) {
    out << "option " << className(a.classification) << ':';
    for (const auto& word : a.spelling) out << ' ' << std::quoted(word);
    out << '\n';
  }
  for (const auto& check : r.checks) {
    out << check.artifact << ": " << check.status << '\n';
    if (check.configuration) {
      const auto& c = *check.configuration;
      out << "  configuration " << c.kind << ":";
      for (const auto& word : c.argv) out << ' ' << std::quoted(word);
      out << "\n  compiler family=" << (c.probe.identity.family == CompilerFamily::gcc ? "gcc" : c.probe.identity.family == CompilerFamily::appleClang ? "apple-clang" : c.probe.identity.family == CompilerFamily::clang ? "clang" : "unknown")
          << " version=" << c.probe.identity.major << '.' << c.probe.identity.minor << '.' << c.probe.identity.patch
          << " triple=" << c.probe.triple << " dialect=" << c.probe.dialect.name << '\n';
      for (const auto& change : c.changes) {
        out << "  option change:";
        for (const auto& word : change.spelling) out << ' ' << std::quoted(word);
        out << ": " << change.reason << '\n';
      }
      if (check.artifact == "raw" || check.artifact == "baseline") out << "  facts: local command; preprocess/build only\n";
      else { out << "  configuration rerun facts:\n"; facts(out, check.satisfied, false); }
    }
  }
  for (const auto& record : r.processes) {
    out << "process " << record.step << " cwd=" << std::quoted(pathToUtf8(record.spec.cwd)) << " argv:";
    for (const auto& word : record.spec.argv) out << ' ' << std::quoted(word);
    out << '\n';
  }
  out << r.diagnostic;
  for (const auto& d : r.resolution.decisions) {
    if (d.original) out << d.original->file << ':' << d.original->line;
    else out << "<generated>:" << d.rawLine;
    out << " #" << d.directive << ": ";
    if (d.decision == Decision::dead) out << "dead region";
    else if (d.decision == Decision::unknown) out << "kept (unknown: " << d.reason << ')';
    else {
      out << "resolved " << (d.decision == Decision::trueValue ? "true" : "false");
      std::string dependencies;
      for (const auto& u : d.external) { if (!dependencies.empty()) dependencies += ", "; dependencies += u.name + ": " + origins(u.fact, profile != nullptr); }
      if (!dependencies.empty()) out << " (" << dependencies << ')';
    }
    out << '\n';
  }
  return out.str();
}
} // namespace distill
