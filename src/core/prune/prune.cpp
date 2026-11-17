// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/prune/prune.h"
#include "core/names/adl.h"
#include "core/names/names.h"
#include <algorithm>
#include <numeric>
#include <queue>

namespace distill {
namespace {
bool intersects(EditRange a, EditRange b) { return a.begin < b.end && b.begin < a.end; }
bool contains(EditRange a, std::size_t p) { return a.begin <= p && p < a.end; }
std::string macroTokens(const Macro& m) {
  auto scan = scanDirectives(m.replacement);
  std::string result = m.functionLike ? "function:" : "object:";
  for (const auto& p : m.parameters) result += p + ',';
  result += m.variadic ? "variadic:" : ":";
  if (!scan.ok()) return "invalid";
  for (const auto& l : scan.value().lines) for (const auto& t : l.tokens) result += t.text + '\n';
  return result;
}
bool equalMacro(const MacroFact& a, const MacroFact& b) {
  return a.state == MacroState::known && b.state == MacroState::known && a.definition && b.definition &&
      macroTokens(*a.definition) == macroTokens(*b.definition);
}
std::string macroValue(const MacroFact& f) {
  if (f.state == MacroState::undefined) return "undefined";
  if (!f.definition || f.state != MacroState::known) return "unknown";
  const auto& m = *f.definition;
  std::string value;
  if (m.functionLike) {
    value = "(";
    for (const auto& p : m.parameters) { if (value.size() > 1) value += ','; value += p; }
    if (m.variadic) { if (value.size() > 1) value += ','; value += "..."; }
    value += ") ";
  }
  return value + (m.replacement.empty() ? "<empty>" : m.replacement);
}
}
Result<std::vector<Token>> pruningIdentifiers(std::string_view text) {
  auto s = scanDirectives(text);
  if (!s.ok()) return s.error();
  auto result = s.value().identifiers;
  // The scanner exposes string/character suffixes separately; pp-number suffixes
  // are part of the number and need their own identifier spelling.
  for (const auto& l : s.value().lines) for (const auto& t : l.tokens) if (t.kind == TokenKind::number) {
    const auto suffix = t.text.find('_');
    if (suffix != t.text.npos) result.push_back({TokenKind::identifier, t.text.substr(suffix), t.begin, t.end, t.line});
    else {
      // Reserved suffixes without '_' are accepted by some compilers too.
      // Conservatively consider each alphabetic start; builtin numeric suffixes
      // and hex digits may over-retain a coincidentally matching indexed name.
      for (std::size_t i = 1; i < t.text.size(); ++i) if ((t.text[i] >= 'a' && t.text[i] <= 'z') || (t.text[i] >= 'A' && t.text[i] <= 'Z'))
        result.push_back({TokenKind::identifier,t.text.substr(i),t.begin,t.end,t.line});
    }
  }
  return result;
}
Result<std::vector<std::size_t>> captureKeepOffsets(std::string_view text) {
  auto scan = scanDirectives(text); if (!scan.ok()) return scan.error();
  std::vector<Token> tokens;
  std::vector<std::size_t> declarations;
  for (const auto& l : scan.value().lines) for (const auto& t : l.tokens) {
    tokens.push_back(t);
    if (!l.directive) declarations.push_back(t.begin);
  }
  // Inspect only trivia gaps, so neither string nor raw-string contents can
  // impersonate a comment. Block comments are skipped as complete units.
  std::vector<std::size_t> result;
  std::size_t previous = 0;
  auto trivia = [&](std::size_t end) {
    for (auto pos = previous; pos < end;) {
      if (text.substr(pos,2) == "/*") {
        auto close = text.find("*/",pos+2); pos = close == text.npos ? end : std::min(end,close+2);
      } else if (text.substr(pos,2) == "//") {
        auto close = text.find('\n',pos+2); if (close == text.npos || close > end) close = end;
        if (text.substr(pos,close-pos).find("distill: keep") != text.npos) {
          auto next = std::lower_bound(declarations.begin(),declarations.end(),close);
          if (next != declarations.end()) result.push_back(*next);
        }
        pos = close;
      } else ++pos;
    }
  };
  for (const auto& t : tokens) { trivia(t.begin); previous = t.end; }
  trivia(text.size()); return result;
}
Result<bool> validatePruningModel(const PruningModel& m, std::string* invalidField) {
  auto invalid = [&](std::string message, std::string field) -> Result<bool> {
    if (invalidField) *invalidField = std::move(field);
    return Error{std::move(message)};
  };
  const auto n = m.families.size();
  if (m.text.size() > 128 * 1024 * 1024) return invalid("pruning model limit","model.text");
  if (n > 1000000) return invalid("pruning model limit","model.families");
  auto valid = [&](EditRange r) { return r.begin < r.end && r.end <= m.text.size(); };
  std::vector<EditRange> ranges;
  for (std::size_t i = 0; i < n; ++i) {
    const auto& f = m.families[i];
    const auto field = "model.families[" + std::to_string(i) + "]";
    if (static_cast<unsigned>(f.kind) > static_cast<unsigned>(FamilyKind::instantiation)) return invalid("invalid family kind",field+".kind");
    for (std::size_t j = 0; j < f.ranges.size(); ++j) {
      auto r = f.ranges[j];
      if (!valid(r)) return invalid("invalid family edit range",field+".ranges["+std::to_string(j)+"]");
      ranges.push_back(r);
    }
    for (std::size_t j = 0; j < f.edges.size(); ++j) if (f.edges[j] >= n) return invalid("invalid AST edge",field+".edges["+std::to_string(j)+"]");
    for (std::size_t j = 0; j < f.associatedTypes.size(); ++j) if (f.associatedTypes[j] >= n) return invalid("invalid association",field+".associatedTypes["+std::to_string(j)+"]");
  }
  std::sort(ranges.begin(), ranges.end(), [](auto a, auto b) { return a.begin < b.begin; });
  for (std::size_t i = 1; i < ranges.size(); ++i) if (intersects(ranges[i-1], ranges[i])) return invalid("overlapping edit ranges","model.families");
  for (std::size_t i = 0; i < m.unions.size(); ++i) {
    auto [a,b] = m.unions[i];
    if (a >= n || b >= n) return invalid("invalid family union","model.unions["+std::to_string(i)+"]");
  }
  for (std::size_t i = 0; i < m.generatedMarkers.size(); ++i) if (!valid(m.generatedMarkers[i])) return invalid("invalid generated marker","model.generatedMarkers["+std::to_string(i)+"]");
  for (std::size_t i = 0; i < m.sourceDirectives.size(); ++i) if (!valid(m.sourceDirectives[i])) return invalid("invalid directive range","model.sourceDirectives["+std::to_string(i)+"]");
  for (std::size_t i = 0; i < m.keepOffsets.size(); ++i) if (m.keepOffsets[i] >= m.text.size()) return invalid("invalid keep offset","model.keepOffsets["+std::to_string(i)+"]");
  for (std::size_t i = 0; i < m.annotations.size(); ++i) {
    auto a = m.annotations[i];
    if (a.family >= n || a.offset > m.text.size()) return invalid("invalid keep annotation","model.annotations["+std::to_string(i)+"]");
  }
  for (std::size_t i = 0; i < m.systemEntities.size(); ++i) {
    const auto& e = m.systemEntities[i];
    const auto field = "model.systemEntities["+std::to_string(i)+"]";
    for (std::size_t j = 0; j < e.userEdges.size(); ++j) if (e.userEdges[j] >= n) return invalid("invalid system-user edge",field+".userEdges["+std::to_string(j)+"]");
    for (std::size_t j = 0; j < e.systemEdges.size(); ++j) if (e.systemEdges[j] >= m.systemEntities.size()) return invalid("invalid system edge",field+".systemEdges["+std::to_string(j)+"]");
  }
  for (std::size_t i = 0; i < m.systemUses.size(); ++i) {
    auto u = m.systemUses[i];
    if ((u.family != noFamily && u.family >= n) || u.entity >= m.systemEntities.size()) return invalid("invalid system use","model.systemUses["+std::to_string(i)+"]");
  }
  auto factValid = [](const MacroFact& f) {
    if (static_cast<unsigned>(f.state) > static_cast<unsigned>(MacroState::unknown)) return false;
    for (auto o : f.provenance) if (static_cast<unsigned>(o) > static_cast<unsigned>(FactOrigin::noProfile)) return false;
    return true;
  };
  std::size_t index = 0;
  for (const auto& [name,fact] : m.judgeInitial) {
    (void)name;
    if (!factValid(fact)) return invalid("invalid judge macro fact","model.judgeInitial["+std::to_string(index)+"].second");
    ++index;
  }
  for (std::size_t i = 0; i < m.expansions.size(); ++i) {
    const auto& e = m.expansions[i];
    const auto field = "model.expansions["+std::to_string(i)+"]";
    if (e.offset > m.text.size()) return invalid("invalid expansion offset",field+".offset");
    if (!factValid(e.parse)) return invalid("invalid expansion fact",field+".parse");
    if (!factValid(e.judge)) return invalid("invalid expansion fact",field+".judge");
  }
  return true;
}
Result<PruningResult> computeRetention(const PruningModel& m) {
  auto valid = validatePruningModel(m); if (!valid.ok()) return valid.error();
  const auto& registry = embeddedAdlRegistry();
  PruningResult out; out.dialect = m.dialect; out.registryVersion = registry.version; out.registryDigest = registry.digest;
  auto disable = [&](std::string s) {
    if (std::find(out.disabledReasons.begin(), out.disabledReasons.end(), s) == out.disabledReasons.end()) out.disabledReasons.push_back(std::move(s));
  };
  for (const auto& why : m.disabledReasons) disable(why);
  if (!registry.supports(m.dialect)) disable("unsupported ADL dialect: " + m.dialect);
  // Preparation carries the resolver's exact decision location in the reasons.
  // Hand-built models without that provenance still fail closed.
  if (m.unknownInUserCode && std::none_of(out.disabledReasons.begin(), out.disabledReasons.end(), [](const auto& why) {
        return why.starts_with("unknown conditional in user code");
      })) disable("unknown conditional in user code");
  for (const auto& header : m.includes) {
    if (!isStandardHeader(header)) disable("non-standard system header: " + header);
    else if (!extensionPruningCovered(header)) disable("unaudited GNU extension header: " + header);
  }
  for (const auto& e : m.expansions) if (e.user && !e.condition) {
    // These two intrinsic replacements are certified by D3b's independent
    // location invariant. Explicit source/command definitions are ordinary
    // macros, and external pasting remains forbidden below.
    const bool locationIntrinsic = (e.name == "__LINE__" || e.name == "__FILE__") &&
        e.parse.state == MacroState::definedUnknown && !e.parse.definition &&
        (e.judge.activeSpecialBuiltin() || (!m.judgeProfile && e.judge.state == MacroState::unknown && !e.judge.definition));
    if (e.predefined && !locationIntrinsic && !equalMacro(e.parse, e.judge)) {
      const auto prefix = "predefined macro " + e.name;
      if (e.judge.state == MacroState::unknown || e.judge.state == MacroState::definedUnknown ||
          (e.judge.state == MacroState::known && !e.judge.definition))
        disable(prefix + ": unknown judge fact");
      else if (e.parse.state == MacroState::unknown || e.parse.state == MacroState::definedUnknown ||
               (e.parse.state == MacroState::known && !e.parse.definition))
        disable(prefix + ": unknown parse fact");
      else disable(prefix + " differs: judge " + macroValue(e.judge) + ", parse " + macroValue(e.parse));
    }
    if (e.paste && (!e.pasteOperandsKnown || e.externalPasteOperand)) disable("external or unknown token-paste operand: " + e.name);
  }
  const auto n = m.families.size(); out.families.resize(n);
  std::vector<FamilyId> parent(n); std::iota(parent.begin(), parent.end(), 0);
  auto find = [&](FamilyId id) { while (parent[id] != id) { parent[id] = parent[parent[id]]; id = parent[id]; } return id; };
  for (auto [a,b] : m.unions) { a = find(a); b = find(b); if (a != b) parent[b] = a; }
  std::vector<std::vector<FamilyId>> groups(n);
  for (FamilyId i = 0; i < n; ++i) groups[find(i)].push_back(i);
  std::queue<FamilyId> work;
  auto keep = [&](FamilyId id, const std::string& why, FamilyId cause) {
    if (out.families[id].kept) return;
    auto initial = cause == noFamily ? id : out.families[cause].initialCause;
    for (auto member : groups[find(id)]) if (!out.families[member].kept) {
      auto& r = out.families[member]; r.kept = true;
      r.reason = member == id ? why : "family aggregation";
      r.cause = member == id ? cause : id; r.initialCause = initial;
      work.push(member);
    }
  };
  const auto adl = registry.names(m.dialect);
  for (FamilyId i = 0; i < n; ++i) {
    const auto& f = m.families[i];
    std::string why;
    if (!out.disabledReasons.empty()) why = "pruning disabled";
    else if (!f.pinned.empty()) why = "pinned: " + f.pinned;
    else if (!f.neverRemoved.empty()) why = "never removed: " + f.neverRemoved;
    else if (f.nonMemberOperator) why = "never removed: operator";
    else if (f.namespaceUsing) why = "never removed: namespace using";
    else if (f.externCDefinition) why = "never removed: extern C definition";
    else if (f.systemSpecialization && f.associatedTypes.empty()) why = "never removed: system specialization without user type";
    else if (std::any_of(f.callableNames.begin(), f.callableNames.end(), [&](const auto& name) { return adl.contains(name); })) why = "never removed: ADL name (A6)";
    else if (!f.root.empty()) why = "root: " + f.root;
    else if (f.kind == FamilyKind::assertion) why = "root: namespace static_assert";
    else if (f.kind == FamilyKind::instantiation) why = "root: explicit instantiation";
    else if (f.names.contains("main")) why = "root: main";
    else if (std::any_of(f.names.begin(), f.names.end(), [&](const auto& name) { return m.keepNames.contains(name); })) why = "root: --keep";
    else if (std::any_of(m.annotations.begin(), m.annotations.end(), [&](auto a) { return a.family == i; })) why = "root: distill: keep";
    else for (auto r : f.ranges) for (auto d : m.sourceDirectives) if (intersects(r,d)) why = "pinned: source directive";
    if (!why.empty()) keep(i, why, noFamily);
  }
  auto tokens = pruningIdentifiers(m.text); if (!tokens.ok()) return tokens.error();
  std::map<std::string, std::vector<FamilyId>, std::less<>> index, callables;
  std::vector<std::vector<FamilyId>> associations(n);
  std::vector<std::vector<std::size_t>> systemUses(n);
  for (FamilyId i = 0; i < n; ++i) {
    for (const auto& name : m.families[i].names) index[name].push_back(i);
    for (const auto& name : m.families[i].callableNames) callables[name].push_back(i);
    const auto& f = m.families[i];
    if (!f.callableNames.empty() || f.kind == FamilyKind::function || f.kind == FamilyKind::specialization || f.systemSpecialization)
      for (auto type : f.associatedTypes) associations[type].push_back(i);
  }
  // Byte ownership, not presumed source locations, decides "outside itself".
  std::vector<std::tuple<std::size_t, std::size_t, FamilyId>> ownership;
  for (FamilyId i = 0; i < n; ++i) for (auto r : m.families[i].ranges) ownership.emplace_back(r.begin,r.end,i);
  std::sort(ownership.begin(), ownership.end());
  std::vector<std::vector<std::string>> lexical(n);
  for (const auto& t : tokens.value()) {
    if (std::any_of(m.generatedMarkers.begin(), m.generatedMarkers.end(), [&](auto r) { return contains(r,t.begin); })) continue;
    const auto it = std::upper_bound(ownership.begin(), ownership.end(), std::tuple{t.begin, std::numeric_limits<std::size_t>::max(), noFamily});
    FamilyId owner = noFamily;
    if (it != ownership.begin()) { auto [begin,end,id] = *std::prev(it); if (begin <= t.begin && t.begin < end) owner = id; }
    if (owner == noFamily) {
      for (auto id : index[t.text]) keep(id, "name: " + t.text + " in structural text", noFamily);
    } else lexical[owner].push_back(t.text);
  }
  auto system = [&](std::size_t start, FamilyId cause) {
    std::vector<bool> seen(m.systemEntities.size()); std::vector<std::size_t> pending{start};
    while (!pending.empty()) {
      auto id = pending.back(); pending.pop_back(); if (seen[id]) continue; seen[id] = true;
      for (auto user : m.systemEntities[id].userEdges) keep(user, "AST: system entity " + m.systemEntities[id].label, cause);
      for (auto next : m.systemEntities[id].systemEdges) pending.push_back(next);
    }
  };
  for (auto u : m.systemUses) {
    if (u.family == noFamily) system(u.entity,noFamily);
    else systemUses[u.family].push_back(u.entity);
  }
  while (!work.empty()) {
    const auto id = work.front(); work.pop(); const auto& f = m.families[id];
    for (const auto& name : lexical[id]) for (auto target : index[name]) if (target != id) keep(target,"name: " + name,id);
    for (auto target : f.edges) keep(target,"AST: referenced by " + f.label,id);
    for (auto target : associations[id]) keep(target,"associated with " + f.label,id);
    for (const auto& name : f.dependentNames) for (auto target : callables[name]) keep(target,"dependent name: " + name,id);
    for (auto entity : systemUses[id]) system(entity,id);
  }
  for (FamilyId i = 0; i < n; ++i) {
    std::size_t bytes = 0; for (auto r : m.families[i].ranges) bytes += r.end - r.begin;
    const auto& r = out.families[i];
    if (r.kept) {
      out.keptBytes += bytes; out.primaryBytes[r.reason] += bytes;
      out.transitiveBytes[out.families[r.initialCause].reason] += bytes;
    } else out.removedBytes += bytes;
  }
  if (std::any_of(out.families.begin(),out.families.end(),[](const auto& f) { return !f.kept; })) out.assumptions.insert("A6");
  return out;
}
Result<bool> staticNameInvariant(const PruningModel& m, const PruningResult& r, std::string_view rendered) {
  if (!r.disabledReasons.empty() || m.families.size() != r.families.size()) return Error{"static invariant requires enabled matching analysis"};
  auto tokens = pruningIdentifiers(rendered); if (!tokens.ok()) return tokens.error();
  std::set<std::string, std::less<>> removed;
  for (FamilyId i = 0; i < m.families.size(); ++i) if (!r.families[i].kept)
    removed.insert(m.families[i].names.begin(),m.families[i].names.end());
  for (const auto& t : tokens.value()) if (removed.contains(t.text)) return Error{"static name invariant: removed name " + t.text};
  return true;
}
} // namespace distill
