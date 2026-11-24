// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/prune/prune.h"
#include "core/names/adl.h"
#include "core/names/names.h"
#include "core/library/library.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <limits>
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
// D3d-4: a single integer-literal token, classified per [lex.icon]. Only
// decimal/octal/hex with standard u/l/ll suffixes; everything else is rejected.
struct IntegerLiteral { std::uint64_t value = 0; bool decimal = false, isUnsigned = false; int length = 0; };
std::optional<IntegerLiteral> integerLiteral(const MacroFact& f) {
  if (f.state != MacroState::known || !f.definition || f.definition->functionLike) return {};
  auto scan = scanDirectives(f.definition->replacement); if (!scan.ok()) return {};
  std::vector<Token> tokens;
  for (const auto& l : scan.value().lines) { if (l.directive) return {}; tokens.insert(tokens.end(),l.tokens.begin(),l.tokens.end()); }
  if (tokens.size() != 1 || tokens[0].kind != TokenKind::number) return {};
  const std::string_view text = tokens[0].text;
  IntegerLiteral out; int base = 10; std::size_t digits = 0;
  if (text.starts_with("0x") || text.starts_with("0X")) { base = 16; digits = 2; }
  else if (text.starts_with('0')) { base = 8; digits = 1; }
  out.decimal = base == 10;
  auto end = digits;
  auto digit = [&](char c) { return base == 16 ? std::isxdigit(static_cast<unsigned char>(c)) != 0 : base == 8 ? c >= '0' && c <= '7' : c >= '0' && c <= '9'; };
  while (end < text.size() && digit(text[end])) ++end;
  if (base != 8 && end == digits) return {};
  if (end > digits) {
    auto parsed = std::from_chars(text.data() + digits, text.data() + end, out.value, base);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + end) return {};
  }
  std::string_view suffix = text.substr(end);
  auto take = [&](std::string_view s) { if (!suffix.starts_with(s)) return false; suffix.remove_prefix(s.size()); return true; };
  auto unsignedSuffix = [&] { return take("u") || take("U"); };
  auto lengthSuffix = [&] { if (take("ll") || take("LL")) return 2; if (take("l") || take("L")) return 1; return 0; };
  out.isUnsigned = unsignedSuffix(); out.length = lengthSuffix();
  if (!out.isUnsigned && out.length) out.isUnsigned = unsignedSuffix();
  if (!suffix.empty()) return {};
  return out;
}
// Type index in [lex.icon] Table 8 order: int, unsigned int, long, unsigned
// long, long long, unsigned long long. Values fitting no standard type (the
// extended-integer-type case) are unclassified.
std::optional<int> literalType(const IntegerLiteral& l, const std::optional<LiteralWidths>& w) {
  if (!w) return {};
  const unsigned bits[] = {w->intBits, w->longBits, w->longLongBits};
  for (auto b : bits) if (b == 0 || b > 64) return {};
  for (int type = 2 * l.length; type < 6; ++type) {
    const bool isUnsigned = type % 2;
    if ((l.isUnsigned && !isUnsigned) || (l.decimal && !l.isUnsigned && isUnsigned)) continue;
    const auto b = bits[type / 2] - (isUnsigned ? 0 : 1);
    const auto max = b >= 64 ? std::numeric_limits<std::uint64_t>::max() : (std::uint64_t{1} << b) - 1;
    if (l.value <= max) return type;
  }
  return {};
}
std::optional<LiteralWidths> judgeWidths(const PruningModel& m) {
  auto number = [&](const char* name) -> std::optional<unsigned> {
    auto i = m.judgeInitial.find(name); if (i == m.judgeInitial.end()) return {};
    const auto& f = i->second;
    // -D/-U can lie about the physical representation (as profileRequirements).
    if (std::find(f.provenance.begin(),f.provenance.end(),FactOrigin::commandOperation) != f.provenance.end()) return {};
    if (f.state != MacroState::known || !f.definition || f.definition->functionLike) return {};
    const auto& s = f.definition->replacement; unsigned n = 0;
    auto parsed = std::from_chars(s.data(), s.data() + s.size(), n);
    if (parsed.ec != std::errc{} || parsed.ptr != s.data() + s.size() || n == 0 || n > 128) return {};
    return n;
  };
  auto cb = number("__CHAR_BIT__"), i = number("__SIZEOF_INT__"), l = number("__SIZEOF_LONG__"), ll = number("__SIZEOF_LONG_LONG__");
  if (!cb || !i || !l || !ll) return {};
  return LiteralWidths{*cb * *i, *cb * *l, *cb * *ll};
}
// Equal value and equal type under both the parse and the judge widths.
bool equalIntegerLiteral(const MacroFact& parse, const MacroFact& judge,
                         const std::optional<LiteralWidths>& parseWidths, const std::optional<LiteralWidths>& judgeModel) {
  auto p = integerLiteral(parse), j = integerLiteral(judge);
  if (!p || !j || p->value != j->value) return false;
  std::optional<int> type;
  for (const auto* l : {&*p, &*j}) for (const auto* w : {&parseWidths, &judgeModel}) {
    auto t = literalType(*l, *w);
    if (!t || (type && *type != *t)) return false;
    type = t;
  }
  return true;
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
    if (f.sourceDeclarationCount > 1000000)
      return invalid("source declaration count limit",field+".sourceDeclarationCount");
    if (f.memberOf != noFamily && (f.memberOf >= n || f.memberOf == i || f.memberName.empty()))
      return invalid("invalid member owner",field+".memberOf");
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
  if (m.parseWidths && (m.parseWidths->intBits == 0 || m.parseWidths->longBits == 0 || m.parseWidths->longLongBits == 0 ||
      std::max({m.parseWidths->intBits, m.parseWidths->longBits, m.parseWidths->longLongBits}) > 128))
    return invalid("invalid parse integer widths","model.parseWidths");
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
    if (e.invocationEnd && (*e.invocationEnd <= e.offset || *e.invocationEnd > m.text.size()))
      return invalid("invalid expansion extent",field+".invocationEnd");
    if (!factValid(e.parse)) return invalid("invalid expansion fact",field+".parse");
    if (!factValid(e.judge)) return invalid("invalid expansion fact",field+".judge");
  }
  for (std::size_t i = 0; i < m.positionUses.size(); ++i)
    if (m.positionUses[i].offset > m.text.size() || m.positionUses[i].what.empty())
      return invalid("invalid position use","model.positionUses["+std::to_string(i)+"]");
  return true;
}
Result<PruningResult> computeRetention(const PruningModel& m) {
  auto valid = validatePruningModel(m); if (!valid.ok()) return valid.error();
  const auto& registry = embeddedAdlRegistry();
  PruningResult out; out.dialect = m.dialect; out.registryVersion = registry.version; out.registryDigest = registry.digest;
  if (!m.assumeInstantiationEffects) out.policy += ", without instantiation-effects";
  if (!m.assumeLibrarySpelledMembers) out.policy += ", without library-spelled-members";
  if (!m.assumeUnspelledFunctions) out.policy += ", without unspelled-functions";
  out.memberDisabledReasons = m.memberDisabledReasons;
  if (!m.assumeLibrarySpelledMembers) out.memberDisabledReasons.push_back("--no-assume=library-spelled-members");
  else if (!m.libraryMemberNamesComputed) out.memberDisabledReasons.push_back("library-spelled identifier set could not be computed");
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
  const auto judgeModel = judgeWidths(m);
  for (const auto& e : m.expansions) if (e.user && !e.condition) {
    // These two intrinsic replacements are certified by D3b's independent
    // location invariant. Explicit source/command definitions are ordinary
    // macros, and external pasting remains forbidden below.
    const bool locationIntrinsic = (e.name == "__LINE__" || e.name == "__FILE__") &&
        e.parse.state == MacroState::definedUnknown && !e.parse.definition &&
        (e.judge.activeSpecialBuiltin() || (!m.judgeProfile && e.judge.state == MacroState::unknown && !e.judge.definition));
    // D3d-4: an equal-value, equal-type integer literal spelled differently is
    // indistinguishable unless its spelling reaches #, ## or a directive.
    const bool equalLiteral = !e.paste && !e.hashOperator &&
        std::none_of(m.sourceDirectives.begin(), m.sourceDirectives.end(), [&](auto r) { return contains(r, e.offset); }) &&
        equalIntegerLiteral(e.parse, e.judge, m.parseWidths, judgeModel);
    if (e.predefined && !locationIntrinsic && !equalMacro(e.parse, e.judge) && !equalLiteral) {
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
  // D3f requires every declaration in an aggregated family to qualify. Shared
  // syntax with a record/enum and any excluded overload veto the whole group.
  std::vector<bool> unspelledEligible(n,true);
  for (FamilyId i = 0; i < n; ++i) {
    const auto& f = m.families[i];
    unspelledEligible[find(i)] = unspelledEligible[find(i)] &&
        f.unspelledFunctionEligible && f.kind == FamilyKind::function &&
        f.memberOf == noFamily && !f.nonMemberOperator && !f.systemSpecialization &&
        !f.externCDefinition && f.pinned.empty() && !f.names.contains("main") &&
        std::none_of(f.callableNames.begin(),f.callableNames.end(),[&](const auto& name) { return adl.contains(name); });
  }
  // Also cover models supplied without worker extraction (and offline tests).
  auto memberRegistry = m.libraryMemberNames;
  const auto scannedMemberNames = embeddedLibraryData().memberNames;
  // The scanned union is already sorted. Query it in place instead of copying
  // every name into a set for each retention calculation.
  // Language member lookup applies even with no system includes.
  for (const char* name : {
      "begin", "end", "get", "get_return_object", "initial_suspend", "final_suspend",
      "return_value", "return_void", "yield_value", "await_transform", "unhandled_exception",
      "get_return_object_on_allocation_failure", "await_ready", "await_suspend", "await_resume"}) memberRegistry.insert(name);
  std::vector<std::vector<FamilyId>> members(n);
  std::vector<std::string> memberReasons(n);
  for (FamilyId i = 0; i < n; ++i) if (m.families[i].memberOf != noFamily) {
    const auto& f = m.families[i];
    members[f.memberOf].push_back(i);
    memberReasons[i] = out.memberDisabledReasons.empty() ? f.memberBlocker : "member pruning disabled";
    if (memberReasons[i].empty() && (memberRegistry.contains(f.memberName) ||
        (m.assumeLibrarySpelledMembers &&
         std::binary_search(scannedMemberNames.begin(),scannedMemberNames.end(),f.memberName))))
      memberReasons[i] = "D3e rule 4: library or language spelling: " + f.memberName;
  }
  // Rules 1-4/8 never separate a unit from a kept owner. Their incoming member
  // dispatch was already covered by D3d-7's whole-record analysis. Keep using
  // that owner analysis rather than inventing an unbounded library hook at
  // the reporting/edit boundary. Include every declaration in a same-name
  // unit (e.g. an overload whose sibling is virtual).
  std::vector<bool> ownerDispatch(n);
  for (FamilyId i = 0; i < n; ++i) {
    const auto& f = m.families[i];
    if (f.memberOf != noFamily && !memberReasons[i].empty())
      for (auto unit : groups[find(i)])
        if (m.families[unit].memberOf == f.memberOf) ownerDispatch[unit] = true;
  }
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
    else if (f.memberOf == noFamily && std::any_of(f.callableNames.begin(), f.callableNames.end(), [&](const auto& name) { return adl.contains(name); })) why = "never removed: ADL name (A6)";
    else if (!f.root.empty()) why = "root: " + f.root;
    else if (f.kind == FamilyKind::assertion) why = "root: namespace static_assert";
    else if (f.kind == FamilyKind::instantiation) why = "root: explicit instantiation";
    else if (f.memberOf == noFamily && f.names.contains("main")) why = "root: main";
    else if ((f.memberOf != noFamily && m.keepNames.contains(f.memberName)) ||
             std::any_of(f.names.begin(), f.names.end(), [&](const auto& name) { return m.keepNames.contains(name); })) why = "root: --keep";
    else if (std::any_of(m.annotations.begin(), m.annotations.end(), [&](auto a) { return a.family == i; })) why = "root: distill: keep";
    else for (auto r : f.ranges) for (auto d : m.sourceDirectives) if (intersects(r,d)) why = "pinned: source directive";
    if (!why.empty()) keep(i, f.memberOf == noFamily ? why : "D3e rule 7: " + why, noFamily);
  }
  // D3e applies to member units of kept owners. A coincidental member name
  // in another record must not resurrect an otherwise-unused record. Actual
  // AST references, roots/pins and effect triggers still retain their owner.
  std::vector<std::optional<std::pair<std::string,FamilyId>>> pendingNames(n);
  auto keepName = [&](FamilyId id, const std::string& why, FamilyId cause) {
    if (out.families[id].kept) return;
    const auto owner = m.families[id].memberOf;
    if (owner != noFamily && !out.families[owner].kept) {
      if (!pendingNames[id]) pendingNames[id] = {why,cause};
    } else keep(id,why,cause);
  };
  auto tokens = pruningIdentifiers(m.text); if (!tokens.ok()) return tokens.error();
  std::map<std::string, std::vector<FamilyId>, std::less<>> index, callables;
  std::map<std::string, std::vector<FamilyId>, std::less<>> effectIndex, effectCallables;
  std::vector<std::vector<FamilyId>> associations(n);
  std::vector<std::vector<std::size_t>> systemUses(n);
  for (FamilyId i = 0; i < n; ++i) {
    for (const auto& name : m.families[i].names) index[name].push_back(i);
    for (const auto& name : m.families[i].callableNames) callables[name].push_back(i);
    const auto& f = m.families[i];
    for (const auto& name : f.names) effectIndex[name].push_back(i);
    for (const auto& name : f.callableNames) effectCallables[name].push_back(i);
    if (f.memberOf != noFamily) {
      index[f.memberName].push_back(i); callables[f.memberName].push_back(i);
      if (!ownerDispatch[i]) {
        effectIndex[f.memberName].push_back(i); effectCallables[f.memberName].push_back(i);
      }
    }
    // Type association retains nonmember ADL/system hooks. Member lookup is
    // bounded separately by D3e names; a kept argument type is not a caller.
    if (f.memberOf == noFamily && (!f.callableNames.empty() || f.kind == FamilyKind::function || f.kind == FamilyKind::specialization || f.systemSpecialization))
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
      for (auto id : index[t.text]) keepName(id, "name: " + t.text + " in structural text", noFamily);
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
  // D3d-7: retaining a template does not retain its instantiation triggers.
  // Reverse the *potential* dependency graph over all text, including dead
  // declarations and patterns, rather than just Clang's seen instantiations.
  // System nodes stay in the graph so aliases/defaults through library types
  // do not break a path. Unseen system dispatch is handled by the refusal below.
  if (std::any_of(m.families.begin(),m.families.end(),[](const auto& f) { return !f.instantiationEffect.empty(); })) {
    const auto total = n + m.systemEntities.size();
    std::vector<std::vector<std::size_t>> incoming(total);
    std::size_t effectEdges = 0;
    auto dependency = [&](std::size_t from, std::size_t to) {
      if (effectEdges < 4000000) { incoming[to].push_back(from); ++effectEdges; }
      else disable("instantiation effect dependency limit");
    };
    for (FamilyId id = 0; id < n; ++id) {
      const auto& f = m.families[id];
      for (auto target : f.edges) dependency(id,target);
      // A whole record did not globally index its non-removable member names.
      // Keep that boundary for owner-dispatched units: their owner/type-carrier
      // paths already cover dispatch, and all their outgoing edges remain here.
      // Independently removable units still need potential member-name edges.
      for (const auto& name : lexical[id]) for (auto target : effectIndex[name]) dependency(id,target);
      for (const auto& name : f.dependentNames) for (auto target : effectCallables[name]) dependency(id,target);
      for (auto target : associations[id]) dependency(id,target);
      for (auto entity : systemUses[id]) dependency(id,n+entity);
      // Record instantiation can instantiate ordinary member declarations too
      // (for example dependent parameter/return types), even without a call.
      // Restore the whole-record potential paths only in the effect graph;
      // this does not force forward retention of unrelated ordinary members.
      for (auto member : members[id]) dependency(id,member);
      // Owner-retained units also reach their owner's effect component.
      if (ownerDispatch[id]) dependency(id,f.memberOf);
    }
    for (auto [a,b] : m.unions) { dependency(a,b); dependency(b,a); }
    for (std::size_t id = 0; id < m.systemEntities.size(); ++id) {
      for (auto target : m.systemEntities[id].userEdges) dependency(n+id,target);
      for (auto target : m.systemEntities[id].systemEdges) dependency(n+id,n+target);
    }
    std::vector<FamilyId> effectCause(total,noFamily);
    std::queue<std::size_t> effects;
    for (FamilyId id = 0; id < n; ++id) if (!m.families[id].instantiationEffect.empty()) {
      effectCause[id] = id; effects.push(id);
      keep(id,"instantiation effect: " + m.families[id].instantiationEffect,noFamily);
    }
    while (!effects.empty()) {
      const auto id = effects.front(); effects.pop();
      const auto cause = effectCause[id];
      if (id < n) {
        const auto& f = m.families[id];
        // A6 bounds names, not which library instantiations dispatch to them.
        // Type-associated hidden friends are covered by the carrier/association
        // graph; free hooks and system specializations have no such bound.
        const bool hook = f.nonMemberOperator ||
            std::any_of(f.callableNames.begin(),f.callableNames.end(),[&](const auto& name) { return adl.contains(name); }) ||
            (f.memberOf != noFamily && !ownerDispatch[id] &&
             (!out.memberDisabledReasons.empty() || m.libraryMemberNames.contains(f.memberName) ||
              std::binary_search(scannedMemberNames.begin(),scannedMemberNames.end(),f.memberName)));
        if (f.systemSpecialization || (hook && (f.kind != FamilyKind::record || !f.boundedSystemHooks)))
          disable("unbounded system instantiation effects through " + f.label);
      }
      for (auto from : incoming[id]) if (effectCause[from] == noFamily) {
        effectCause[from] = cause; effects.push(from);
        if (from < n) keep(from,"instantiation effect: possible trigger of " + m.families[cause].label,cause);
      }
    }
  }
  if (!out.disabledReasons.empty()) for (FamilyId id = 0; id < n; ++id) keep(id,"pruning disabled",noFamily);
  auto closure = [&](bool legacyAssociations) {
    while (!work.empty()) {
      const auto id = work.front(); work.pop(); const auto& f = m.families[id];
      if (f.memberOf != noFamily) keep(f.memberOf,"AST: referenced by " + f.label,id);
      for (auto member : members[id]) {
        if (!memberReasons[member].empty()) keep(member,memberReasons[member],id);
        if (pendingNames[member]) keep(member,pendingNames[member]->first,pendingNames[member]->second);
      }
      for (const auto& name : lexical[id]) for (auto target : index[name]) if (target != id) keepName(target,"name: " + name,id);
      for (auto target : f.edges) keep(target,"AST: referenced by " + f.label,id);
      for (auto target : associations[id])
        if (legacyAssociations || !unspelledEligible[find(target)]) keep(target,"associated with " + f.label,id);
      for (const auto& name : f.dependentNames) for (auto target : callables[name]) keepName(target,"dependent name: " + name,id);
      for (auto entity : systemUses[id]) system(entity,id);
    }
  };
  closure(!m.assumeUnspelledFunctions);
  if (m.assumeUnspelledFunctions && out.disabledReasons.empty() &&
      std::any_of(out.families.begin(),out.families.end(),[](const auto& f) { return !f.kept; })) {
    // Exact old-rule counterfactual, starting from the new closure. Roots and
    // rule-6 effects are identical, so only the forward fixpoint is replayed.
    auto actual = out.families;
    for (FamilyId i = 0; i < n; ++i) if (actual[i].kept) work.push(i);
    closure(true);
    for (FamilyId i = 0; i < n; ++i)
      if (!actual[i].kept && out.families[i].kept && unspelledEligible[find(i)]) {
        out.rule3UnkeptDeclarations += m.families[i].sourceDeclarationCount;
        for (auto range : m.families[i].ranges) out.rule3UnkeptBytes += range.end-range.begin;
      }
    out.families = std::move(actual);
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
  // Even an enum or unused template can seed constant-evaluation caches.
  // Every enabled pruning result relies on A7, including a pruned artifact
  // whose families all survived (rendering may still remove empty shells).
  // A11 excludes friend instantiation and parameter-object/constexpr destruction,
  // plus destruction of never-reached block storage, even in empty-shell rendering.
  if (out.disabledReasons.empty()) {
    out.assumptions.insert("A7");
    if (m.assumeUnspelledFunctions) out.assumptions.insert("D3f");
    if (m.assumeInstantiationEffects) out.assumptions.insert("A11");
    if (m.assumeUnobservedGuardMacros) out.assumptions.insert("A14");
    if (out.memberDisabledReasons.empty()) out.assumptions.insert("A12");
  }
  return out;
}
Result<bool> staticNameInvariant(const PruningModel& m, const PruningResult& r, std::string_view rendered) {
  if (!r.disabledReasons.empty() || m.families.size() != r.families.size()) return Error{"static invariant requires enabled matching analysis"};
  auto tokens = pruningIdentifiers(rendered); if (!tokens.ok()) return tokens.error();
  std::set<std::string, std::less<>> removed;
  for (FamilyId i = 0; i < m.families.size(); ++i) if (!r.families[i].kept) {
    // Members of a removed record disappear under whole-record pruning; a
    // same-named member elsewhere is allowed. D3e's additional invariant is
    // about individually removed units of records that remain in the output.
    const auto owner = m.families[i].memberOf;
    if (owner == noFamily || r.families[owner].kept) {
      removed.insert(m.families[i].names.begin(),m.families[i].names.end());
      if (owner != noFamily) removed.insert(m.families[i].memberName);
    }
  }
  for (const auto& t : tokens.value()) if (removed.contains(t.text)) return Error{"static name invariant: removed name " + t.text};
  return true;
}
} // namespace distill
