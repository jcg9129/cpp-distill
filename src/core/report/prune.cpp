// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/report/prune.h"
#include "core/names/adl.h"
#include <iomanip>
#include <sstream>
namespace distill {
Result<std::string> renderPruningReport(const PruningModel& m, const PruningResult& r) {
  if (m.families.size() != r.families.size()) return Error{"pruning report model/result mismatch"};
  std::ostringstream out;
  out << "pruning analysis (does not certify an artifact)\npolicy: " << r.policy
      << "\nADL registry: " << r.registryVersion << ' ' << r.registryDigest << "\ndialect coverage: " << r.dialect << "\nparse configuration:";
  for (const auto& a : m.parseArgv) out << ' ' << std::quoted(a);
  out << '\n';
  for (const auto& a : m.untranslatedOptions) out << "untranslated parse option: " << a << '\n';
  for (const auto& why : r.disabledReasons) out << "pruning disabled: " << why << '\n';
  for (const auto& a : r.assumptions) out << "pruning assumption: " << a << '\n';
  out << "extension coverage:\n" << embeddedExtensionCoverage();
  for (FamilyId id = 0; id < m.families.size(); ++id) {
    const auto& f = m.families[id]; const auto& kept = r.families[id];
    out << "family " << id << ' ' << std::quoted(f.label) << ": " << kept.reason;
    if (kept.cause != noFamily) out << "; cause=" << kept.cause;
    if (kept.initialCause != noFamily) out << "; initial=" << kept.initialCause;
    if (f.original) out << "; " << f.original->file << ':' << f.original->line;
    out << '\n';
  }
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
