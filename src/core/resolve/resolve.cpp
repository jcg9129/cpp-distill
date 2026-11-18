// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/resolve/resolve.h"
#include "core/lex/scan.h"
#include "core/path.h"
#include "core/validate/preprocess.h"
#include <algorithm>
#include <functional>
#include <sstream>

namespace distill {
namespace {
constexpr std::size_t maxEnumerated = 4; // At most 2^4 evaluator calls per directive.
template<class T> T take(Result<T> r) { if (!r.ok()) throw r.error(); return std::move(r.value()); }
bool opening(std::string_view n) { return n == "if" || n == "ifdef" || n == "ifndef"; }
bool conditional(std::string_view n) { return opening(n) || n == "elif" || n == "elifdef" || n == "elifndef" || n == "else" || n == "endif"; }
bool origin(const MacroFact& f, FactOrigin o) { return std::find(f.provenance.begin(), f.provenance.end(), o) != f.provenance.end(); }
std::string definition(const std::string& name, const Macro& m) {
  std::string s = "#define " + name;
  if (m.functionLike) {
    s += '(';
    for (std::size_t i = 0; i < m.parameters.size(); ++i) { if (i) s += ','; s += m.parameters[i]; }
    s += ')';
  }
  return s + ' ' + m.replacement + '\n';
}
std::vector<Token> tokens(std::string_view s) {
  auto scan = take(scanDirectives(s)); std::vector<Token> t;
  for (const auto& l : scan.lines) t.insert(t.end(), l.tokens.begin(), l.tokens.end());
  return t;
}
std::string spell(const std::vector<Token>& t) {
  std::string s;
  for (const auto& x : t) { if (!s.empty()) s += ' '; s += x.text; }
  return s;
}
using States = std::map<std::string, MacroFact, std::less<>>;
struct Group { std::vector<std::size_t> branches; std::size_t end = 0; };
struct Engine {
  const InlineResult& raw;
  const CompileCommand& cmd;
  ConditionEvaluator& evaluator;
  const ResolveOptions& options;
  EvaluationRequirements requirements;
  ResolveResult result;
  ScanResult scan;
  std::set<std::string, std::less<>> names, userDefines, featureNames;
  std::map<std::size_t, Group> groups;
  std::map<std::size_t, std::size_t> records;
  std::vector<SourceLocation> locations;
  std::vector<bool> keep, resync;
  std::map<std::size_t, SourceLocation> markerAt; // Generated markers by first line.
  PresumedFile presumed; // Of the output, for short markers.
  std::size_t verbatim = 0; // Depth of enclosing demoted blocks.
  Engine(const InlineResult& r, const CompileCommand& c, ConditionEvaluator& e, const ResolveOptions& o)
      : raw(r), cmd(c), evaluator(e), options(o), requirements(o.requirements) {}
  MacroFact initial(const std::string& name) const {
    if (userDefines.contains(name)) return {MacroState::undefined, {}, {FactOrigin::a3}};
    if (options.profile) return options.profile->initialState(name);
    // Dialect is the one external fact available without a judge profile.
    if (name == "__cplusplus" && requirements.dialect) {
      const auto& s = *requirements.dialect;
      long value = s.ends_with("26") ? 202400 : s.ends_with("23") ? 202302 : s.ends_with("20") ? 202002 :
                   s.ends_with("17") ? 201703 : s.ends_with("14") ? 201402 : s.ends_with("11") ? 201103 :
                   s.ends_with("98") || s.ends_with("03") ? 199711 : 0;
      if (value) return {MacroState::known, Macro{false, {}, false, std::to_string(value) + "L"}, {FactOrigin::commandOperation}};
    }
    return {MacroState::unknown, {}, {FactOrigin::noProfile}};
  }
  void checkDeadline() const {
    if (options.deadline && std::chrono::steady_clock::now() >= *options.deadline) throw Error{"resolver deadline exceeded"};
  }
  void prepare() {
    checkDeadline();
    scan = take(scanDirectives(raw.text));
    if (options.profile) {
      auto inherited = profileRequirements(*options.profile);
      if (!requirements.dialect) requirements.dialect = inherited.dialect;
      if (!requirements.integers) requirements.integers = inherited.integers;
    } else if (!requirements.dialect) requirements.dialect = cmd.dialect();
    for (const auto& t : scan.identifiers) names.insert(t.text);
    if (options.profile) for (const auto& [name, f] : options.profile->snapshotFacts) {
      names.insert(name);
      if (f.definition) for (const auto& t : tokens(f.definition->replacement)) if (t.kind == TokenKind::identifier) names.insert(t.text);
    }
    if (options.profile) {
      for (const auto& [name, m] : options.profile->judgeMacros) {
        names.insert(name);
        for (const auto& t : tokens(m.replacement)) if (t.kind == TokenKind::identifier) names.insert(t.text);
      }
      const auto dependencies = nativeDependencies(options.profile->compiler);
      for (const auto& [name, scope] : dependencies) { (void)scope; featureNames.insert(name); }
      if (dependencies.empty()) for (const auto& name : names) if (isReserved(name)) featureNames.insert(name);
    } else {
      // Inventory filenames, rather than hard-coded versions, select every model.
      for (const auto& [file, text] : embeddedProfileFiles()) if (file.starts_with("native/")) {
        std::istringstream lines(text); std::string line;
        while (std::getline(lines, line)) if (!line.empty() && !line.starts_with('#')) featureNames.insert(line.substr(0, line.find(' ')));
      }
    }
    for (const char* n : {"__OPTIMIZE__", "__OPTIMIZE_SIZE__", "__NO_INLINE__"}) featureNames.insert(n);
    names.insert(featureNames.begin(), featureNames.end());
    std::vector<std::size_t> stack;
    keep.assign(scan.lines.size(), true); resync.assign(scan.lines.size() + 1, false);
    // Recover static source coordinates even for generated once guards (null map).
    SourceLocation at{pathToUtf8(cmd.source), 1};
    std::set<std::size_t> markers;
    std::map<std::size_t, std::string_view> directives;
    for (const auto& l : scan.lines) if (l.directive) {
      if (generatedMarker(l)) markers.insert(l.firstLine);
      else directives[l.firstLine] = l.directive->name;
    }
    std::vector<SourceLocation> physical;
    std::istringstream lines(raw.text); std::string line;
    PresumedFile input; // Short markers take the file of their segment's marker.
    while (std::getline(lines, line)) {
      physical.push_back(at);
      if (markers.contains(physical.size())) {
        if (!take(input.parse(line, at))) throw Error{"invalid generated line marker"};
        markerAt[physical.size()] = at;
      } else {
        if (auto d = directives.find(physical.size()); d != directives.end()) input.directive(d->second);
        ++at.line;
      }
    }
    for (std::size_t i = 0; i < scan.lines.size(); ++i) {
      const auto& l = scan.lines[i];
      locations.push_back(physical.at(l.firstLine - 1));
      if (l.firstLine <= raw.lineMap.size() && raw.lineMap[l.firstLine - 1]) locations.back() = *raw.lineMap[l.firstLine - 1];
      if (!l.directive) continue;
      const auto& d = *l.directive;
      if (d.name == "define" && !d.tokens.empty() && !isReserved(d.tokens[0].text)) userDefines.insert(d.tokens[0].text);
      if (conditional(d.name)) {
        auto rawLine = l.tokens.front().line;
        records[i] = result.decisions.size();
        std::optional<SourceLocation> original;
        if (rawLine <= raw.lineMap.size()) original = raw.lineMap[rawLine - 1];
        result.decisions.push_back({rawLine, original, d.name, Decision::dead, "dead region", {}, {}});
      }
      if (opening(d.name)) {
        if (stack.size() >= options.maxNesting) throw Error{"resolver nesting limit exceeded"};
        groups[i].branches.push_back(i); stack.push_back(i);
      } else if (d.name == "elif" || d.name == "elifdef" || d.name == "elifndef" || d.name == "else") {
        if (stack.empty() || scan.lines[groups.at(stack.back()).branches.back()].directive->name == "else") throw Error{"unbalanced conditional"};
        groups.at(stack.back()).branches.push_back(i);
      } else if (d.name == "endif") {
        if (stack.empty()) throw Error{"unbalanced #endif"};
        groups.at(stack.back()).end = i; stack.pop_back();
      }
    }
    if (!stack.empty()) throw Error{"unbalanced #if"};
    const auto rawIdentifier = [&](std::string_view name) { return std::any_of(scan.identifiers.begin(), scan.identifiers.end(), [&](const auto& t) { return t.text == name; }); };
    if (raw.flags.counter || rawIdentifier("__COUNTER__")) result.disabledReasons.push_back("__COUNTER__");
    if (raw.flags.pragmaOperator || rawIdentifier("_Pragma")) result.disabledReasons.push_back("_Pragma");
    if (raw.flags.pushMacro) result.disabledReasons.push_back("push_macro");
    if (raw.flags.popMacro) result.disabledReasons.push_back("pop_macro");
    for (const auto& l : scan.lines) if (l.directive && l.directive->name == "pragma" && !l.directive->tokens.empty()) {
      const auto& n = l.directive->tokens[0].text;
      if ((n == "push_macro" || n == "pop_macro") && std::find(result.disabledReasons.begin(), result.disabledReasons.end(), n) == result.disabledReasons.end()) result.disabledReasons.push_back(n);
    }
    for (const auto& r : cmd.resolutionAllowed().reasons) result.disabledReasons.push_back(r);
    if (options.profile) {
      auto a3 = options.profile->checkA3(userDefines);
      if (!a3.ok()) result.disabledReasons.push_back(a3.error().message);
    }
  }
  ResolutionDecision& record(std::size_t i) { return result.decisions.at(records.at(i)); }
  void decide(std::size_t i, Decision d, std::string reason = {}) { record(i).decision = d; record(i).reason = std::move(reason); }
  void condition(std::size_t i, const States& state) {
    checkDeadline();
    auto& r = record(i); const auto& d = *scan.lines[i].directive;
    std::vector<Token> expression = d.tokens;
    if (d.name != "if" && d.name != "elif") {
      if (expression.size() != 1 || expression[0].kind != TokenKind::identifier) { decide(i, Decision::unknown, "malformed condition"); return; }
      expression = tokens((d.name == "ifndef" || d.name == "elifndef" ? "!defined(" : "defined(") + expression[0].text + ")");
    }
    std::map<std::string, bool, std::less<>> closure; // true = value used, false = defined only
    bool booleanOnly = true, dialectSensitive = false, invocation = false;
    std::size_t visited = 0, closureDepth = 0;
    std::string reason;
    // Unknown names used only under `defined` (docs/reviews/2026-11-18-boolean-precision.md).
    std::vector<std::string> enumerated;
    std::function<void(const std::vector<Token>&, const std::set<std::string, std::less<>>&)> visit;
    visit = [&](const auto& ts, const auto& bound) {
      checkDeadline();
      if (closureDepth >= options.maxNesting) { reason = "condition limit"; return; }
      ++closureDepth;
      for (std::size_t j = 0; j < ts.size() && reason.empty(); ++j) {
        if (++visited > options.maxConditionTokens) { reason = "condition limit"; break; }
        const auto& t = ts[j];
        if (t.text == "##" || t.text == "%:%:") { reason = "token paste"; break; }
        if (t.kind != TokenKind::identifier) {
          if (t.text != "0" && t.text != "1" && t.text != "!" && t.text != "&&" && t.text != "||" && t.text != "(" && t.text != ")") booleanOnly = false;
          if (t.kind == TokenKind::literal || (t.kind == TokenKind::number && t.text.find('\'') != t.text.npos)) dialectSensitive = true;
          continue;
        }
        bool value = true; std::string name = t.text;
        if (name == "defined") {
          ++j; if (j < ts.size() && ts[j].text == "(") ++j;
          if (j == ts.size() || ts[j].kind != TokenKind::identifier) { reason = "malformed defined"; break; }
          name = ts[j].text; value = false;
          if (j + 1 < ts.size() && ts[j + 1].text == ")") ++j;
        } else if (name == "and" || name == "or" || name == "not" || name == "bitand" || name == "bitor" || name == "xor" || name == "compl" || name == "not_eq") { dialectSensitive = true; booleanOnly = false; continue; }
        if (bound.contains(name)) continue;
        const auto found = state.find(name);
        const auto f = found == state.end() ? initial(name) : found->second;
        const auto [entry, fresh] = closure.emplace(name, value);
        const bool expand = value && (fresh || !entry->second);
        entry->second = entry->second || value;
        if (value && (name == "true" || name == "false") && f.state == MacroState::undefined) { dialectSensitive = true; continue; }
        if (value && name.starts_with("__has_") && j + 1 < ts.size() && ts[j + 1].text == "(" && f.state != MacroState::known) { reason = "builtin invocation: " + name; break; }
        if (f.state == MacroState::unknown) {
          // `defined U` is 0 or 1 before macro replacement whatever U expands
          // to; a value use can change the parse, so it is never enumerated.
          if (!value) { if (std::find(enumerated.begin(), enumerated.end(), name) == enumerated.end()) enumerated.push_back(name); continue; }
          reason = "unknown macro: " + name; break;
        }
        if (value && f.state == MacroState::definedUnknown) { reason = (f.activeSpecialBuiltin() ? "builtin invocation: " : "unknown definition: ") + name; break; }
        if (expand && f.state == MacroState::known) {
          if (!f.definition) { reason = "missing definition: " + name; break; }
          invocation = invocation || f.definition->functionLike;
          std::set<std::string, std::less<>> parameters;
          for (const auto& p : f.definition->parameters) parameters.insert(p.ends_with("...") ? p.substr(0, p.size() - 3) : p);
          if (f.definition->variadic) { parameters.insert("__VA_ARGS__"); parameters.insert("__VA_OPT__"); dialectSensitive = true; }
          const auto body = tokens(f.definition->replacement);
          // Macro-generated defined has implementation-dependent/undefined semantics.
          if (std::any_of(body.begin(), body.end(), [](const auto& x) { return x.text == "defined"; })) { reason = "macro-generated defined"; break; }
          visit(body, parameters);
        }
      }
      --closureDepth;
    };
    visit(expression, {});
    std::string source;
    for (const auto& [name, value] : closure) {
      const auto found = state.find(name); const auto f = found == state.end() ? initial(name) : found->second;
      // Decisions are invariant under every value of an enumerated name; no fact is used.
      if (!value && f.state == MacroState::unknown) continue;
      const bool external = std::any_of(f.provenance.begin(), f.provenance.end(), [](auto o) {
        return o != FactOrigin::source && o != FactOrigin::systemHeader && o != FactOrigin::pragma && o != FactOrigin::join;
      });
      if (external) r.external.push_back({name, f, value});
      if (f.state == MacroState::known) source += definition(name, *f.definition);
      else if (f.state == MacroState::definedUnknown) source += "#define " + name + " 1\n";
    }
    // In a macro argument, `defined U` is macro-generated and U may be expanded
    // before `defined` sees it; only names outside any invocation are exact.
    if (reason.empty() && !enumerated.empty() && (invocation || enumerated.size() > maxEnumerated)) reason = "unknown macro: " + enumerated.front();
    // A lone (negated) `defined U` (#ifdef/#ifndef) depends on U: no evaluator call.
    if (reason.empty() && !enumerated.empty() && std::all_of(expression.begin(), expression.end(), [&](const Token& t) {
          return t.text == "defined" || t.text == "!" || t.text == "(" || t.text == ")" || t.text == enumerated.front();
        }) && std::count_if(expression.begin(), expression.end(), [](const Token& t) { return t.text == "defined"; }) == 1)
      reason = "unknown macro: " + enumerated.front();
    if (reason.empty() && !booleanOnly && !requirements.integers) reason = "unknown integer model";
    if (reason.empty() && dialectSensitive && !requirements.dialect) reason = "unknown dialect";
    if (!reason.empty()) { decide(i, Decision::unknown, reason); return; }
    // Boolean-only trees are independent of target widths; no requirement recorded.
    if (!booleanOnly) r.requirements.integers = requirements.integers;
    if (dialectSensitive) r.requirements.dialect = requirements.dialect;
    const auto conditionText = spell(expression);
    std::string defines;
    for (const auto& name : enumerated) defines += "#define " + name + " 1\n";
    auto unique = [&](std::string name) {
      while (source.find(name) != source.npos || defines.find(name) != defines.npos || conditionText.find(name) != conditionText.npos) name += '_';
      return name;
    };
    const auto yes = unique("DISTILL_CONDITION_TRUE"), no = unique("DISTILL_CONDITION_FALSE");
    const auto test = "#if " + conditionText + '\n' + yes + "\n#else\n" + no + "\n#endif\n";
    // Every definedness combination must evaluate, without error, to one value.
    std::optional<bool> value;
    for (std::size_t mask = 0; mask < (std::size_t{1} << enumerated.size()); ++mask) {
      checkDeadline();
      std::string variant = source;
      for (std::size_t k = 0; k < enumerated.size(); ++k) if (mask >> k & 1) variant += "#define " + enumerated[k] + " 1\n";
      auto evaluated = evaluator.evaluate({variant + test, r.requirements, yes, no, options.deadline});
      if (!evaluated.ok()) { decide(i, Decision::unknown, "evaluator error: " + evaluated.error().message); return; }
      if (value && *value != evaluated.value()) { decide(i, Decision::unknown, "unknown macro: " + enumerated.front()); return; }
      value = evaluated.value();
    }
    decide(i, *value ? Decision::trueValue : Decision::falseValue);
  }
  void transition(std::size_t i, States& state) {
    const auto& l = scan.lines[i]; if (!l.directive) return;
    const auto& d = *l.directive; const auto& t = d.tokens;
    if (d.name == "define" && !t.empty()) {
      // Test phase-2 adjacency without treating comments as empty gaps.
      bool function = false;
      if (t.size() > 1 && t[1].text == "(") {
        const auto gap = std::string_view(raw.text).substr(t[0].end, t[1].begin - t[0].end);
        const auto joined = tokens("a" + std::string(gap) + "b");
        function = joined.size() == 1 && joined[0].text == "ab";
      }
      std::string s = "#define " + t[0].text;
      std::size_t j = 1;
      if (function) {
        for (; j < t.size(); ++j) { s += t[j].text; if (t[j].text == ")") { ++j; break; } }
      }
      auto table = take(parseMacros(s + '\n'));
      table.at(t[0].text).replacement = spell({t.begin() + j, t.end()});
      state[t[0].text] = {MacroState::known, table.at(t[0].text), {FactOrigin::source}};
    } else if (d.name == "undef" && !t.empty()) state[t[0].text] = {MacroState::undefined, {}, {FactOrigin::source}};
    else if (d.name == "include" && !t.empty()) {
      const auto header = t[0].text.substr(1, t[0].text.size() - 2);
      const bool standard = isStandardHeader(header);
      for (auto& [name, f] : state) {
        // Source writes (including an equal-valued branch join) do not inherit
        // A2's external identity guarantee. Neither do earlier invalidations.
        const bool externalFact = !f.provenance.empty() && std::all_of(f.provenance.begin(), f.provenance.end(), [](auto o) {
          return o != FactOrigin::source && o != FactOrigin::systemHeader && o != FactOrigin::pragma && o != FactOrigin::join;
        });
        const bool identity = options.profile && externalFact && f.state != MacroState::unknown &&
            isIdentityMacro(name, options.profile->libraries);
        bool builtin = false;
        if (options.profile) {
          const auto external = options.profile->initialState(name);
          builtin = (external.state == MacroState::known || external.state == MacroState::definedUnknown) &&
              (origin(external, FactOrigin::compilerBuiltin) || origin(external, FactOrigin::specialBuiltin) || origin(external, FactOrigin::nativeModel));
        } else builtin = name == "__cplusplus" && requirements.dialect.has_value();
        if (isIdentityMacroName(name)) builtin = builtin && externalFact;
        if (!standard || (!identity && !builtin && !options.names.protectedUserMacro(name, options.profile ? options.profile->libraries : std::vector<std::string>{})))
          f = {MacroState::unknown, {}, {FactOrigin::systemHeader}};
      }
    } else if (d.name == "pragma" && t.size() >= 2 && t[0].text == "GCC" &&
        (t[1].text == "target" || t[1].text == "optimize" || t[1].text == "push_options" || t[1].text == "pop_options")) {
      for (const auto& name : featureNames) state[name] = {MacroState::unknown, {}, {FactOrigin::pragma}};
    }
  }
  States join(const std::vector<States>& paths) {
    auto state = paths.front();
    for (std::size_t j = 1; j < paths.size(); ++j) for (auto& [name, f] : state) {
      const auto& other = paths[j].at(name);
      if (f.state != other.state || f.definition != other.definition) f = {MacroState::unknown, {}, {FactOrigin::join}};
      else for (auto o : other.provenance) if (!origin(f, o)) f.provenance.push_back(o);
    }
    return state;
  }
  void remove(std::size_t a, std::size_t b) { for (auto i = a; i < b; ++i) keep[i] = false; resync[b] = true; }
  void walk(std::size_t a, std::size_t b, States& state) {
    for (auto i = a; i < b; ++i) {
      checkDeadline();
      if (!groups.contains(i)) { transition(i, state); continue; }
      const auto& g = groups.at(i);
      bool unknown = false, selected = false;
      // Demotion: the first forced alternative (a forced #else/#endif forces
      // the whole block); inside a demoted block every alternative is forced.
      std::optional<std::size_t> forced;
      if (verbatim) forced = 0;
      const auto& demote = options.forcedUnknown;
      if (demote.contains(record(g.end).rawLine)) forced = 0;
      for (std::size_t j = 0; j < g.branches.size() && !forced; ++j) if (demote.contains(record(g.branches[j]).rawLine))
        forced = scan.lines[g.branches[j]].directive->name == "else" ? 0 : j;
      std::vector<std::size_t> possible;
      for (std::size_t j = 0; j < g.branches.size(); ++j) {
        const auto branch = g.branches[j]; const auto& d = *scan.lines[branch].directive;
        if (selected) continue;
        if (unknown) decide(branch, Decision::unknown, "prior unknown condition");
        else if (forced == j) decide(branch, Decision::unknown, verbatim ? "inside demoted conditional" : "demoted: configuration disagrees");
        else if (d.name == "else") decide(branch, Decision::trueValue);
        else condition(branch, state);
        const auto choice = record(branch).decision;
        if (choice == Decision::unknown) unknown = true;
        if (choice != Decision::falseValue) possible.push_back(j);
        if (!unknown && choice == Decision::trueValue) selected = true;
      }
      decide(g.end, unknown ? Decision::unknown : Decision::trueValue, unknown ? "unknown block" : "");
      if (unknown) {
        std::vector<States> paths;
        const bool nested = forced.has_value();
        // Forced blocks keep their contents verbatim (identical to raw), so no
        // decision only the judge's alternative would exercise is made here.
        if (nested) ++verbatim;
        for (auto j : possible) {
          auto path = state;
          walk(g.branches[j] + 1, j + 1 < g.branches.size() ? g.branches[j + 1] : g.end, path);
          paths.push_back(std::move(path));
        }
        if (nested) --verbatim;
        if (scan.lines[g.branches.back()].directive->name != "else") paths.push_back(state);
        state = join(paths);
      } else {
        const auto live = possible.empty() ? g.end : g.branches[possible.front()];
        for (std::size_t j = 0; j < g.branches.size(); ++j) {
          const auto start = g.branches[j], end = j + 1 < g.branches.size() ? g.branches[j + 1] : g.end;
          remove(start, start + 1);
          if (start == live) walk(start + 1, end, state);
          else remove(start + 1, end);
        }
        remove(g.end, g.end + 1);
      }
      // A marker in a skipped alternative has no effect. Restore after EVERY
      // switch/end of a retained enclosing group containing any deletion.
      if (std::any_of(keep.begin() + i, keep.begin() + g.end + 1, [](bool k) { return !k; })) {
        for (auto branch : g.branches) if (branch != i) resync[branch + 1] = true;
        resync[g.end + 1] = true;
      }
      i = g.end;
    }
  }
  void append(std::string_view text, std::optional<SourceLocation> at = {}) {
    if (text.size() > options.maxOutputBytes - result.text.size()) throw Error{"resolver output size limit exceeded"};
    for (char c : text) {
      if (result.text.empty() || result.text.back() == '\n') result.lineMap.push_back(at);
      result.text += c;
      if (c == '\n' && at) ++at->line;
    }
  }
  bool generatedMarker(const LogicalLine& l) const {
    return l.directive && l.directive->name == "line" && l.firstLine <= raw.lineMap.size() && !raw.lineMap[l.firstLine-1];
  }
  void marker(const SourceLocation& at) {
    if (!result.text.empty() && result.text.back() != '\n') append("\n");
    append(presumed.marker(at));
  }
  void render() {
    for (std::size_t i = 0; i < scan.lines.size(); ++i) if (keep[i]) {
      checkDeadline();
      const auto& l = scan.lines[i];
      // Raw markers already reset the following line; an extra one is safe.
      if (resync[i]) marker(locations[i]);
      // Re-spell kept generated markers: deletions may have changed their segment.
      if (generatedMarker(l)) { marker(markerAt.at(l.firstLine)); continue; }
      if (l.directive) presumed.directive(l.directive->name);
      std::size_t physical = l.firstLine;
      for (auto start = l.begin; start < l.end;) {
        auto end = raw.text.find('\n', start); end = end == raw.text.npos ? l.end : std::min(end + 1, l.end);
        std::optional<SourceLocation> at;
        if (physical <= raw.lineMap.size()) at = raw.lineMap[physical - 1];
        auto keepOffset = std::lower_bound(raw.keepOffsets.begin(),raw.keepOffsets.end(),start);
        for (; keepOffset != raw.keepOffsets.end() && *keepOffset < end; ++keepOffset)
          result.keepOffsets.push_back(result.text.size() + *keepOffset - start);
        append(std::string_view(raw.text).substr(start, end - start), at);
        start = end; ++physical;
      }
    }
    if (resync.back() && !scan.lines.empty()) {
      auto at = locations.back();
      const auto line = scan.lines.back().lastLine;
      if (line <= raw.lineMap.size() && raw.lineMap[line - 1]) at = *raw.lineMap[line - 1];
      ++at.line; marker(at);
    }
  }
  ResolveResult run() {
    prepare();
    if (!result.disabledReasons.empty()) {
      result.text = raw.text; result.lineMap = raw.lineMap; result.keepOffsets = raw.keepOffsets;
      for (auto& d : result.decisions) { d.decision = Decision::unknown; d.reason = "resolution disabled"; }
    } else {
      States state;
      for (const auto& name : names) state.emplace(name, initial(name));
      walk(0, scan.lines.size(), state); render();
    }
    for (const auto& d : result.decisions) {
      const auto key = d.decision == Decision::trueValue ? "true" : d.decision == Decision::falseValue ? "false" : d.decision == Decision::dead ? "dead" : "unknown: " + d.reason;
      ++result.counts[key];
      if (d.decision == Decision::unknown) result.unknownInUserCode = true;
    }
    return std::move(result);
  }
};
} // namespace
Result<ResolveResult> resolveConditions(const InlineResult& raw, const CompileCommand& cmd, ConditionEvaluator& evaluator, const ResolveOptions& options) {
  try { return Engine{raw, cmd, evaluator, options}.run(); }
  catch (const Error& e) { return e; }
  catch (const std::exception& e) { return Error{std::string("resolve: ") + e.what()}; }
  catch (...) { return Error{"resolve: unknown failure"}; }
}
std::vector<std::size_t> disagreements(const ResolveResult& a, const ResolveResult& b) {
  std::vector<std::size_t> found;
  if (!a.disabledReasons.empty() || !b.disabledReasons.empty() || a.decisions.size() != b.decisions.size()) return found;
  for (std::size_t i = 0; i < a.decisions.size(); ++i) {
    const auto& x = a.decisions[i]; const auto& y = b.decisions[i];
    if (x.rawLine != y.rawLine || x.original != y.original || x.directive != y.directive) return {};
    if ((x.decision == Decision::trueValue || x.decision == Decision::falseValue) && y.decision != Decision::dead && x.decision != y.decision)
      found.push_back(i);
  }
  return found;
}
Result<ResolveResult> demoteDisagreements(const InlineResult& raw, const CompileCommand& cmd, ConditionEvaluator& evaluator,
                                          ResolveOptions options, ResolveResult resolution, const ResolveResult& observed,
                                          std::vector<ResolutionDecision>* demoted) {
  // The forced set grows strictly, so this ends after at most one round per directive.
  for (std::size_t round = 0; !sameDecisions(resolution, observed); ++round) {
    if (round > resolution.decisions.size()) return Error{"demotion did not converge"};
    if (options.deadline && std::chrono::steady_clock::now() >= *options.deadline) return Error{"demotion deadline exceeded"};
    const auto found = disagreements(resolution, observed);
    std::size_t added = 0;
    for (auto i : found) if (options.forcedUnknown.insert(resolution.decisions[i].rawLine).second) {
      ++added;
      if (demoted) demoted->push_back(resolution.decisions[i]);
    }
    if (!added) return Error{"configuration disagreement cannot be demoted"};
    auto rerun = resolveConditions(raw, cmd, evaluator, options);
    if (!rerun.ok()) return rerun.error();
    resolution = std::move(rerun.value());
    if (!resolution.disabledReasons.empty()) return Error{"resolution disabled during demotion"};
  }
  return resolution;
}
bool sameDecisions(const ResolveResult& a, const ResolveResult& b) {
  if (!a.disabledReasons.empty() || !b.disabledReasons.empty() || a.decisions.size() != b.decisions.size()) return false;
  for (std::size_t i = 0; i < a.decisions.size(); ++i) {
    const auto& x = a.decisions[i]; const auto& y = b.decisions[i];
    if (x.rawLine != y.rawLine || x.original != y.original || x.directive != y.directive) return false;
    if ((x.decision == Decision::trueValue || x.decision == Decision::falseValue) && y.decision != Decision::dead && x.decision != y.decision) return false;
  }
  return true;
}
} // namespace distill
