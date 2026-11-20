// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/report/prune.h"
#include "core/names/adl.h"
#include "core/library/library.h"
#include <iomanip>
#include <numeric>
#include <sstream>
namespace distill {
Result<std::string> renderPruningReport(const PruningModel& m, const PruningResult& r) {
  if (m.families.size() != r.families.size()) return Error{"pruning report model/result mismatch"};
  std::ostringstream out;
  out << "pruning analysis (does not certify an artifact)\npolicy: " << r.policy
      << "\nADL registry: " << r.registryVersion << ' ' << r.registryDigest << "\ndialect coverage: " << r.dialect << "\nparse configuration:";
  for (const auto& a : m.parseArgv) out << ' ' << std::quoted(a);
  out << '\n';
  out << "member pruning: " << (r.memberDisabledReasons.empty() && r.disabledReasons.empty() ? "enabled" : "disabled") << '\n';
  out << "library-spelled identifier names: " << m.libraryMemberNames.size() << '\n';
  out << "library member inventory: " << embeddedLibraryData().memberInventoryProvenance << '\n';
  for (const auto& why : r.memberDisabledReasons) out << "member pruning disabled: " << why << '\n';
  for (const auto& a : m.untranslatedOptions) out << "untranslated parse option: " << a << '\n';
  for (const auto& why : r.disabledReasons) out << "pruning disabled: " << why << '\n';
  if (!m.assumeInstantiationEffects)
    out << "pruning assumption disabled: A11 instantiation-effects (--no-assume=instantiation-effects)\n";
  if (!m.assumeLibrarySpelledMembers)
    out << "pruning assumption disabled: A12 library-spelled-members (--no-assume=library-spelled-members)\n";
  for (const auto& a : r.assumptions) out << "pruning assumption: " << assumptionLabel(a) << '\n';
  out << "extension coverage:\n" << embeddedExtensionCoverage();
  for (FamilyId id = 0; id < m.families.size(); ++id) {
    const auto& f = m.families[id]; const auto& kept = r.families[id];
    out << "family " << id << ' ' << std::quoted(f.label) << ": " << kept.reason;
    if (kept.cause != noFamily) out << "; cause=" << kept.cause;
    if (kept.initialCause != noFamily) out << "; initial=" << kept.initialCause;
    if (f.original) out << "; " << f.original->file << ':' << f.original->line;
    out << '\n';
  }
  // Same-name overloads, redeclarations and specializations are a single unit.
  // Ordinary family lines above retain the per-declaration provenance.
  std::vector<FamilyId> parent(m.families.size()); std::iota(parent.begin(),parent.end(),0);
  auto find = [&](FamilyId id) { while (parent[id] != id) { parent[id] = parent[parent[id]]; id = parent[id]; } return id; };
  for (auto [a,b] : m.unions) { a = find(a); b = find(b); if (a != b) parent[b] = a; }
  std::map<std::pair<FamilyId,std::string>,std::vector<FamilyId>> units;
  for (FamilyId id = 0; id < m.families.size(); ++id) if (m.families[id].memberOf != noFamily)
    units[{find(id),m.families[id].memberName}].push_back(id);
  std::size_t keptUnits = 0, removedUnits = 0;
  for (const auto& [key,ids] : units) {
    (void)key;
    const auto id = ids.front(); const auto& f = m.families[id]; const auto& kept = r.families[id];
    if (kept.kept) ++keptUnits; else ++removedUnits;
    auto cause = id;
    for (std::size_t steps = 0; steps < m.families.size() && r.families[cause].reason == "family aggregation" && r.families[cause].cause != noFamily; ++steps)
      cause = r.families[cause].cause;
    auto reason = r.families[cause].reason;
    if (!kept.kept && reason.starts_with("removed: ")) reason.erase(0,9);
    if (kept.kept && reason.starts_with("name:")) reason = "D3e rule 5: " + reason;
    else if (kept.kept && (reason.starts_with("AST:") || reason.starts_with("dependent name:"))) reason = "D3e rule 6: " + reason;
    std::size_t bytes = 0;
    for (auto part : ids) for (auto range : m.families[part].ranges) bytes += range.end-range.begin;
    out << "member unit " << id << ' ' << std::quoted(f.label) << ' ' << std::quoted(f.memberName)
        << ": " << (kept.kept ? "blocked: " : "removed: ") << reason << "; bytes=" << bytes << '\n';
  }
  out << "member units kept=" << keptUnits << " removed=" << removedUnits << '\n';
  out << "unique family bytes kept=" << r.keptBytes << " removed=" << r.removedBytes << '\n';
  const auto structural = m.text.size() - r.keptBytes - r.removedBytes;
  out << "structural source bytes " << structural << " (shells, directives, trivia and provenance markers; separate from family ownership)\n";
  out << "primary structural bytes " << structural << " structural retention\n";
  out << "transitive structural bytes " << structural << " structural retention\n";
  for (const auto& [reason,bytes] : r.primaryBytes) out << "primary bytes " << std::quoted(reason) << ' ' << bytes << '\n';
  for (const auto& [reason,bytes] : r.transitiveBytes) out << "transitive bytes " << std::quoted(reason) << ' ' << bytes << '\n';
  return out.str();
}
}
