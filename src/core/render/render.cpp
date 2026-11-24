// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/render/render.h"
#include "core/names/names.h"
#include "core/validate/preprocess.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <span>

namespace distill {
namespace {
template<class T> T take(Result<T> r) { if (!r.ok()) throw r.error(); return std::move(r.value()); }
bool contains(EditRange a, EditRange b) { return a.begin <= b.begin && b.end <= a.end; }
bool balancedDelimiters(std::span<const Token> tokens) {
  std::vector<char> stack;
  for (const auto& t : tokens) {
    const auto& v = t.text;
    if (v == "(") stack.push_back(')');
    else if (v == "[" || v == "<:") stack.push_back(']');
    else if (v == "{" || v == "<%") stack.push_back('}');
    else if (v == ")" || v == "]" || v == ":>" || v == "}" || v == "%>") {
      const char close = v == ":>" ? ']' : v == "%>" ? '}' : v.front();
      if (stack.empty() || stack.back() != close) return false;
      stack.pop_back();
    }
  }
  return stack.empty();
}
struct Input {
  const PruningModel& model;
  ScanResult scan;
  std::vector<EditRange> deletions, markers, trimmedDirectives;
  TrimmingStats trimming;
  std::vector<SourceLocation> locations;
  std::map<std::size_t, SourceLocation> markerAt; // Parsed generated markers by begin offset.
  Input(const PruningModel& m, const PruningResult* r) : model(m), scan(take(scanDirectives(m.text))) {
    take(validatePruningModel(m));
    if (r) {
      if (!r->disabledReasons.empty() || r->families.size() != m.families.size())
        throw Error{"rendering requires enabled matching retention"};
      for (FamilyId id = 0; id < m.families.size(); ++id) if (!r->families[id].kept)
        deletions.insert(deletions.end(),m.families[id].ranges.begin(),m.families[id].ranges.end());
    }
    joinDeletions();
    if (r) removeEmptyShells();
    joinDeletions();
    // Validate declaration/shell ownership before trimming can exempt directives.
    for (const auto& l : scan.lines)
      if (l.directive && !generatedMarker(l) && !touching({l.begin,l.end}).empty())
        throw Error{"deletion touches source directive #" + l.directive->name + " at line " + std::to_string(l.firstLine)};
    if (r) trimPreprocessor();
    joinDeletions();
    std::map<std::size_t, std::string_view> directives; // Other directives by physical start.
    for (const auto& l : scan.lines) {
      const bool generated = generatedMarker(l);
      if (generated) markers.push_back({l.begin,l.end});
      else if (l.directive) directives[l.begin] = l.directive->name;
      for (const auto& t : l.tokens) for (auto d : touching({t.begin,t.end}))
        if (!contains(d,{t.begin,t.end})) throw Error{"deletion splits token '" + t.text + "' at byte " + std::to_string(t.begin)};
      if (l.directive && !generated && !touching({l.begin,l.end}).empty() &&
          std::find(trimmedDirectives.begin(),trimmedDirectives.end(),EditRange{l.begin,l.end}) == trimmedDirectives.end())
        throw Error{"deletion touches source directive #" + l.directive->name + " at line " + std::to_string(l.firstLine)};
    }
    for (auto c : scan.comments) for (auto d : touching({c.begin,c.end}))
      if (!contains(d,{c.begin,c.end})) throw Error{"deletion splits comment at byte " + std::to_string(c.begin)};
    // Interpret only lexer-recognized markers; a marker-shaped raw-string line
    // is ordinary source. D2 provenance overrides the presumed coordinates.
    // An unset presumed location must never invent an internal artifact name.
    // Inliner output establishes the original file before generated guards.
    // Short markers are checked against their segment (PresumedFile).
    SourceLocation at{"",1};
    PresumedFile presumed;
    auto directive = directives.begin();
    std::size_t physical = 1;
    for (std::size_t begin = 0; begin < m.text.size(); ++physical) {
      auto end = m.text.find('\n',begin); if (end == m.text.npos) end = m.text.size();
      for (; directive != directives.end() && directive->first < begin; ++directive) presumed.directive(directive->second);
      locations.push_back(physical <= m.lineMap.size() && m.lineMap[physical-1] ? *m.lineMap[physical-1] : at);
      if (std::binary_search(markers.begin(),markers.end(),EditRange{begin,end + (end < m.text.size())},
          [](auto a,auto b) { return a.begin < b.begin; })) {
        auto parsed = presumed.parse(std::string_view(m.text).substr(begin,end-begin),at);
        if (!parsed.ok()) throw Error{"invalid generated marker at line " + std::to_string(physical) + ": " + parsed.error().message};
        if (!parsed.value()) throw Error{"invalid generated marker at line " + std::to_string(physical)};
        markerAt[begin] = at;
      } else ++at.line;
      begin = end + (end < m.text.size());
    }
  }
  bool generatedMarker(const LogicalLine& l) const {
    return l.directive && l.directive->name == "line" &&
        l.firstLine <= model.lineMap.size() && !model.lineMap[l.firstLine-1];
  }
  void joinDeletions() {
    std::sort(deletions.begin(),deletions.end(),[](auto a,auto b) { return a.begin < b.begin; });
    // Adjacent ownership partitions may together own a single syntactic token.
    std::vector<EditRange> joined;
    for (auto d : deletions) {
      if (!joined.empty() && joined.back().end >= d.begin) joined.back().end = std::max(joined.back().end,d.end);
      else joined.push_back(d);
    }
    deletions = std::move(joined);
  }
  void trimPreprocessor() {
    if (!model.assumeIdempotentStandardHeaders && !model.assumeUnobservedGuardMacros) return;
    // Plan whole logical-line deletions in immutable flat coordinates, after
    // declaration/shell deletion. Never apply this to raw or flat rendering.
    auto drop = [&](const LogicalLine& l, bool include) {
      trimmedDirectives.push_back({l.begin,l.end});
      const auto lines = l.lastLine-l.firstLine+1, bytes = l.end-l.begin;
      if (include) { trimming.includeLines += lines; trimming.includeBytes += bytes; }
      else { trimming.guardLines += lines; trimming.guardBytes += bytes; }
    };
    std::map<std::size_t,std::vector<const PruneMacroExpansion*>> expansions;
    for (const auto& e : model.expansions)
      if (!deleted({e.offset,e.offset+1})) expansions[e.offset].push_back(&e);
    struct ScopeDefinition { bool safe = false, functionLike = false; };
    std::map<std::string,ScopeDefinition,std::less<>> definitions;
    std::map<std::string,bool,std::less<>> safeReplacements;
    auto safeReplacement = [](std::span<const Token> tokens) {
      return balancedDelimiters(tokens) && std::none_of(tokens.begin(),tokens.end(),[](const auto& t) {
        return t.text == "#" || t.text == "##" || t.text == "%:" || t.text == "%:%:";
      });
    };
    auto scopeFact = [&](const MacroFact& fact) -> std::optional<ScopeDefinition> {
      if (fact.state != MacroState::known || !fact.definition) return {};
      const auto& replacement = fact.definition->replacement;
      if (auto it = safeReplacements.find(replacement); it != safeReplacements.end())
        return ScopeDefinition{it->second,fact.definition->functionLike};
      // A leading # or %: belongs to the replacement, not a directive.
      const auto parsed = scanDirectives("0 " + replacement);
      std::vector<Token> tokens;
      if (parsed.ok()) for (const auto& line : parsed.value().lines)
        tokens.insert(tokens.end(),line.tokens.begin(),line.tokens.end());
      const bool safe = safeReplacements[replacement] = parsed.ok() && safeReplacement(tokens);
      return ScopeDefinition{safe,fact.definition->functionLike};
    };
    // Keep source invocation extents across logical lines. Directives and
    // deleted tokens cannot supply evidence for a surviving invocation.
    std::vector<Token> sourceTokens;
    std::set<std::size_t> directiveTokens;
    for (const auto& l : scan.lines) for (const auto& t : l.tokens) {
      sourceTokens.push_back(t);
      if (l.directive) directiveTokens.insert(t.begin);
    }
    auto balancedInvocation = [&](const PruneMacroExpansion& e) {
      auto it = std::lower_bound(sourceTokens.begin(),sourceTokens.end(),e.offset,
          [](const auto& t,auto offset) { return t.begin < offset; });
      // Rescanned function-like names need a source spelling too. An alias or
      // synthesized name at this offset does not prove its consumed extent.
      if (it == sourceTokens.end() || it->begin != e.offset || it->text != e.name) return false;
      const auto begin = std::size_t(it-sourceTokens.begin());
      if (begin+1 == sourceTokens.size() || sourceTokens[begin+1].text != "(") return false;
      std::size_t depth = 0, argument = begin+2;
      for (auto i = begin+1; i < sourceTokens.size(); ++i) {
        const auto& t = sourceTokens[i];
        if (directiveTokens.contains(t.begin) || deleted({t.begin,t.end})) return false;
        if (t.text == "(") ++depth;
        else if (t.text == ")") {
          if (!--depth)
            return balancedDelimiters(std::span(sourceTokens).subspan(argument,i-argument)) &&
                balancedDelimiters(std::span(sourceTokens).subspan(begin,i-begin+1));
        } else if (t.text == "," && depth == 1) {
          // Preprocessing arguments split on commas outside parentheses;
          // braces and brackets do not protect a comma from that split.
          if (!balancedDelimiters(std::span(sourceTokens).subspan(argument,i-argument))) return false;
          argument = i+1;
        }
      }
      return false;
    };
    auto scopePreservingExpansion = [&](const PruneMacroExpansion& e) {
      const auto parse = scopeFact(e.parse), judge = scopeFact(e.judge);
      auto safe = [&](const ScopeDefinition& d) {
        return d.safe && (!d.functionLike || balancedInvocation(e));
      };
      // Expansion facts describe separate parse and judge environments. If
      // either is populated, require both; a fallback cannot hide an unknown.
      if (e.parse.state != MacroState::unknown || e.judge.state != MacroState::unknown ||
          !e.parse.provenance.empty() || !e.judge.provenance.empty())
        return parse && judge && safe(*parse) && safe(*judge);
      if (auto it = definitions.find(e.name); it != definitions.end()) return safe(it->second);
      if (auto it = model.judgeInitial.find(e.name); it != model.judgeInitial.end())
        if (const auto fact = scopeFact(it->second)) return safe(*fact);
      return false;
    };
    bool reservedMacroWrite = false, macroPragma = false, survivingConditional = false;
    for (const auto& l : scan.lines) {
      if (l.directive) {
        const auto& d = *l.directive;
        survivingConditional |= d.name == "if" || d.name == "ifdef" || d.name == "ifndef" ||
            d.name == "elif" || d.name == "elifdef" || d.name == "elifndef";
        if ((d.name == "define" || d.name == "undef") && !d.tokens.empty()) {
          const auto& name = d.tokens.front().text;
          reservedMacroWrite |= isReserved(name);
        }
        macroPragma |= d.name == "pragma" && !d.tokens.empty() &&
            (d.tokens.front().text == "push_macro" || d.tokens.front().text == "pop_macro");
      }
      for (const auto& t : l.tokens)
        if (!deleted({t.begin,t.end}) && t.kind == TokenKind::identifier && t.text == "_Pragma") macroPragma = true;
    }
    std::set<std::string> seen;
    std::size_t braces = 0, parens = 0, brackets = 0;
    bool uncertain = false;
    const Token* previous = nullptr;
    enum class BlockKind { other, namespaceBlock, linkageBlock };
    std::vector<BlockKind> blocks; // One entry per surviving brace depth.
    enum class Header { none, namespaceName, namespaceTail, nestedName, externLanguage, linkageBrace };
    Header header = Header::none;
    bool previousShellClose = false;
    auto outsideFamilies = [&](const Token& t) {
      return std::none_of(model.families.begin(),model.families.end(),[&](const auto& f) {
        return std::any_of(f.ranges.begin(),f.ranges.end(),[&](auto r) { return r.begin < t.end && t.begin < r.end; });
      });
    };
    // Ranges may own trailing whitespace. Since no surviving ordinary token
    // intervenes, this must be the range's final surviving token.
    auto familyBoundary = [&](std::size_t begin) {
      return !previous || previousShellClose || std::any_of(model.families.begin(),model.families.end(),[&](const auto& f) {
        return std::any_of(f.ranges.begin(),f.ranges.end(),[&](auto r) {
          return r.begin <= previous->begin && previous->end <= r.end && r.end <= begin;
        });
      });
    };
    for (const auto& l : scan.lines) {
      if (generatedMarker(l)) continue;
      if (l.directive) {
        const auto& d = *l.directive;
        if ((d.name == "define" || d.name == "undef") && !d.tokens.empty()) {
          const auto& ts = d.tokens;
          std::size_t body = 1;
          bool functionLike = false, parametersClosed = true;
          if (d.name == "define" && ts.size() > 1 && ts[1].text == "(") {
            // Adjacency is after phase-2 splicing, not in physical offsets.
            const auto gap = scanDirectives(std::string_view(model.text).substr(ts[0].end,ts[1].begin-ts[0].end));
            functionLike = gap.ok() && std::all_of(gap.value().lines.begin(),gap.value().lines.end(),
                [](const auto& line) { return line.text.empty(); });
          }
          if (functionLike) {
            while (body < ts.size() && ts[body].text != ")") ++body;
            parametersClosed = body < ts.size();
            if (parametersClosed) ++body;
          }
          definitions[ts[0].text] = {d.name == "define" && parametersClosed && safeReplacement(std::span(ts).subspan(body)),functionLike};
        }
        if (d.name == "if" || d.name == "ifdef" || d.name == "ifndef" ||
            d.name == "elif" || d.name == "elifdef" || d.name == "elifndef")
          // Alternatives may open different scopes. Never infer scope after
          // a surviving conditional from just the worker's active parse.
          uncertain = true;
        if (model.assumeIdempotentStandardHeaders && !reservedMacroWrite && !macroPragma && !uncertain &&
            !braces && !parens && !brackets && d.name == "include" &&
            d.tokens.size() == 1 && d.tokens[0].kind == TokenKind::header && familyBoundary(l.begin) &&
            std::none_of(model.families.begin(),model.families.end(),[&](const auto& f) {
              return std::any_of(f.ranges.begin(),f.ranges.end(),[&](auto r) { return r.begin < l.end && l.begin < r.end; });
            })) {
          // The first occurrence must pass exactly the same scope and family
          // boundary checks as a removal; an ineligible include never seeds seen.
          const auto& spelling = d.tokens[0].text;
          if (spelling.starts_with('<') && spelling.ends_with('>')) {
            const auto h = std::string_view(spelling).substr(1,spelling.size()-2);
            // C .h headers can implement partial-inclusion protocols. A1 also
            // lists extensions; only simple C++ library names qualify here.
            if (h != "cassert" && !h.ends_with(".h") && h.find('/') == h.npos && isStandardHeader(h) &&
                !seen.insert(spelling).second) drop(l,true);
          }
        }
        continue;
      }
      // Balance alone cannot prove substitution preserves source nesting:
      // require balanced arguments and consumed invocation tokens too, forbid
      // stringification/pasting, and check every nested/rescanned expansion.
      for (auto it = expansions.lower_bound(l.begin); it != expansions.end() && it->first < l.end; ++it)
        for (const auto* e : it->second) if (!scopePreservingExpansion(*e)) uncertain = true;
      for (const auto& t : l.tokens) if (!deleted({t.begin,t.end})) {
        const auto& v = t.text;
        BlockKind opening = BlockKind::other;
        // Recognize the namespace/linkage grammar as tokens arrive. Only an
        // opening at depth zero outside declaration ownership certifies a close.
        if (!braces && !parens && !brackets &&
            (header != Header::none || v == "namespace" || v == "extern") && outsideFamilies(t)) {
          const auto pending = header;
          header = Header::none;
          if (pending == Header::namespaceName && (v == "{" || v == "<%")) opening = BlockKind::namespaceBlock;
          else if ((pending == Header::namespaceName || pending == Header::nestedName) && t.kind == TokenKind::identifier && v != "inline") header = Header::namespaceTail;
          else if (pending == Header::namespaceTail && v == "::") header = Header::nestedName;
          else if (pending == Header::nestedName && v == "inline") header = Header::nestedName;
          else if (pending == Header::namespaceTail && (v == "{" || v == "<%")) opening = BlockKind::namespaceBlock;
          else if (pending == Header::externLanguage && (v == "\"C\"" || v == "\"C++\"")) header = Header::linkageBrace;
          else if (pending == Header::linkageBrace && (v == "{" || v == "<%")) opening = BlockKind::linkageBlock;
          else if (v == "namespace" && (!previous || previous->text != "using")) header = Header::namespaceName;
          else if (v == "extern") header = Header::externLanguage;
        } else header = Header::none;
        previousShellClose = false;
        // Nesting is an extra veto, never evidence of a declaration boundary.
        if (v == "(") ++parens;
        else if (v == ")" && parens) --parens;
        else if (v == "[" || v == "<:") ++brackets;
        else if ((v == "]" || v == ":>") && brackets) --brackets;
        else if (v == "{" || v == "<%") { ++braces; blocks.push_back(opening); }
        else if ((v == "}" || v == "%>") && braces) {
          --braces;
          previousShellClose = !braces && blocks.back() != BlockKind::other;
          blocks.pop_back();
        }
        previous = &t;
      }
    }
    // Count every surviving identifier, including all directive operands and
    // macro replacements. Pragma strings are excluded artifact-wide above.
    if (model.assumeUnobservedGuardMacros && !survivingConditional && !macroPragma) {
      constexpr std::string_view guardPrefix = "DISTILL_ONCE_";
      std::map<std::string,std::size_t,std::less<>> uses;
      for (const auto& l : scan.lines)
        if (std::find(trimmedDirectives.begin(),trimmedDirectives.end(),EditRange{l.begin,l.end}) == trimmedDirectives.end())
          for (const auto& t : l.tokens) if (t.kind == TokenKind::identifier && t.text.starts_with(guardPrefix) && !deleted({t.begin,t.end})) ++uses[t.text];
      // Token pasting can observe a guard without spelling its complete name
      // in the source. Keep observed surviving expansions as well.
      for (const auto& e : model.expansions)
        if (e.name.starts_with(guardPrefix) && !deleted({e.offset,e.offset+1})) ++uses[e.name];
      for (const auto& l : scan.lines) if (l.directive) {
        const auto& d = *l.directive;
        if (d.name == "define" && d.tokens.size() == 1) {
          const auto& name = d.tokens[0].text;
          const bool generated = l.lastLine <= model.lineMap.size() &&
              std::all_of(model.lineMap.begin()+l.firstLine-1,model.lineMap.begin()+l.lastLine,[](const auto& at) { return !at; });
          if (generated && name.starts_with(guardPrefix) && name.size() == guardPrefix.size()+16 &&
              name.find_first_not_of("0123456789abcdef",guardPrefix.size()) == name.npos && uses[name] == 1) drop(l,false);
        }
      }
    }
    deletions.insert(deletions.end(),trimmedDirectives.begin(),trimmedDirectives.end());
  }
  void removeEmptyShells() {
    // Shells do not participate in retention. Ignore comments/literals as
    // names and generated line markers as contents. Source directives and
    // other surviving non-shell tokens always keep their enclosing shells.
    struct Lexeme { const Token* token; bool directive; };
    std::vector<Lexeme> ts;
    for (const auto& l : scan.lines) if (!generatedMarker(l))
      for (const auto& t : l.tokens) ts.push_back({&t,bool(l.directive)});
    const auto n = ts.size();
    std::vector<std::size_t> close(n,n), stack;
    for (std::size_t i = 0; i < n; ++i) if (!ts[i].directive) {
      if (ts[i].token->text == "{") stack.push_back(i);
      else if (ts[i].token->text == "}" && !stack.empty()) { close[stack.back()] = i; stack.pop_back(); }
    }
    // Namespace identities are shared by reopened blocks. Linkage shells use
    // their enclosing scope; anonymous/inline namespaces also permit lookup
    // through their parent.
    struct Namespace {
      std::size_t parent;
      std::string name;
      bool transparent = false, exposed = false;
      std::size_t firstUse;
      std::vector<std::size_t> blocks, inlineBlocks;
    };
    struct Shell {
      std::size_t begin, end, parent, scope;
      bool contents = false, keep = false;
    };
    const auto none = n;
    std::vector<Namespace> namespaces{{0,"",true,false,n,{},{}}};
    std::map<std::pair<std::size_t,std::string>,std::size_t> identities;
    std::vector<Shell> shells;
    std::vector<std::size_t> parents, scope(n), owner(n,none);
    std::vector<bool> header(n), live(n), exposes(n);
    std::set<std::string,std::less<>> ambiguousQualifiers, namespaceNames;
    for (std::size_t i = 0; i < n; ++i)
      live[i] = !deleted({ts[i].token->begin,ts[i].token->end});
    for (std::size_t i = 0; i < n; ++i) {
      while (!parents.empty() && shells[parents.back()].end < i) parents.pop_back();
      const auto parent = parents.empty() ? none : parents.back();
      const auto enclosing = parent == none ? 0 : shells[parent].scope;
      scope[i] = enclosing; owner[i] = parent;
      if (ts[i].directive) { exposes[i] = true; continue; }
      auto j = i+1;
      std::vector<std::pair<std::size_t,bool>> names;
      const bool isNamespace = ts[i].token->text == "namespace";
      if (isNamespace) {
        if (j < n && ts[j].token->kind == TokenKind::identifier) {
          names.push_back({j++,i && ts[i-1].token->text == "inline"});
          while (j < n && ts[j].token->text == "::") {
            ++j;
            const bool inlined = j < n && ts[j].token->text == "inline";
            if (inlined) ++j;
            if (j == n || ts[j].token->kind != TokenKind::identifier) break;
            names.push_back({j++,inlined});
          }
        }
        // Alias targets and using-directives can expose descendant names via
        // other spellings. Treat their reach conservatively, including macros.
        if ((j < n && ts[j].token->text == "=") ||
            (i && ts[i-1].token->text == "using")) {
          for (auto k = i+1; k < n && ts[k].token->text != ";"; ++k) exposes[k] = true;
          if (j < n && ts[j].token->text == "=") ambiguousQualifiers.insert(ts[i+1].token->text);
        }
      } else if (ts[i].token->text == "extern" && j < n &&
                 ts[j].token->kind == TokenKind::literal &&
                 (ts[j].token->text == "\"C\"" || ts[j].token->text == "\"C++\"")) ++j;
      else continue;
      if (j == n || ts[j].directive || ts[j].token->text != "{" || close[j] == n) continue;
      auto begin = i;
      if (isNamespace && i && !ts[i-1].directive && ts[i-1].token->text == "inline") --begin;
      auto current = enclosing;
      auto addNamespace = [&](const std::string& name,bool transparent) {
        auto [it,inserted] = identities.emplace(std::make_pair(current,name),namespaces.size());
        if (inserted) namespaces.push_back({current,name,transparent,false,n,{},{}});
        current = it->second;
        namespaces[current].transparent = namespaces[current].transparent || transparent;
        namespaces[current].blocks.push_back(shells.size());
        if (transparent && !name.empty()) namespaces[current].inlineBlocks.push_back(shells.size());
      };
      if (isNamespace) {
        if (names.empty()) addNamespace("",true);
        for (auto [k,inlined] : names) addNamespace(ts[k].token->text,inlined);
      }
      for (auto k = begin; k <= j; ++k) header[k] = true;
      header[close[j]] = true;
      shells.push_back({begin,close[j],parent,current});
      parents.push_back(shells.size()-1);
      // Skip the header, but give all its tokens the enclosing lookup scope.
      for (auto k = i; k <= j; ++k) { scope[k] = enclosing; owner[k] = parent; }
      i = j;
    }
    std::map<std::string,std::vector<std::size_t>,std::less<>> uses;
    for (std::size_t i = 0; i < n; ++i) if (live[i] && !header[i]) {
      if (owner[i] != none) shells[owner[i]].contents = true;
      if (ts[i].token->kind == TokenKind::identifier) {
        uses[ts[i].token->text].push_back(i);
        if (ts[i].directive) ambiguousQualifiers.insert(ts[i].token->text);
      }
    }
    for (const auto& ns : namespaces) namespaceNames.insert(ns.name);
    // Match a qualified spelling relative to each enclosing lookup scope.
    // Anonymous and inline namespaces may be omitted from the spelling.
    auto matches = [&](auto&& self,std::size_t id,std::size_t base,
                       const std::vector<std::string>& name,std::size_t count) -> bool {
      if (!count) {
        if (id == base) return true;
        return id && namespaces[id].transparent && self(self,namespaces[id].parent,base,name,0);
      }
      if (!id || id == base) return false;
      const auto& ns = namespaces[id];
      return (ns.name == name[count-1] && self(self,ns.parent,base,name,count-1)) ||
          (ns.transparent && self(self,ns.parent,base,name,count));
    };
    auto reaches = [&](std::size_t id,std::size_t i) {
      // At global scope, any surviving spelling is enough to keep the name.
      // Restrict nested names to uses that can reach their enclosing scope.
      if (!namespaces[id].parent || ts[i].directive) return true;
      std::vector<std::string> name{ts[i].token->text};
      auto first = i;
      while (first >= 2 && live[first-1] && live[first-2] &&
             ts[first-1].token->text == "::" && ts[first-2].token->kind == TokenKind::identifier) {
        first -= 2; name.push_back(ts[first].token->text);
      }
      std::reverse(name.begin(),name.end());
      const bool global = first && live[first-1] && ts[first-1].token->text == "::";
      // An alias or macro qualifier may denote an exposed ancestor. A known,
      // different namespace qualification does not become relevant merely
      // because this namespace was imported elsewhere.
      if (namespaces[id].exposed)
        for (std::size_t k = 0; k+1 < name.size(); ++k)
          if (ambiguousQualifiers.contains(name[k]) || !namespaceNames.contains(name[k])) return true;
      auto base = global ? 0 : scope[i];
      do {
        if (matches(matches,id,base,name,name.size())) return true;
        if (!base) break;
        base = namespaces[base].parent;
      } while (true);
      if (!global) for (auto p = namespaces[id].parent; p; p = namespaces[p].parent)
        if (namespaces[p].exposed && matches(matches,id,p,name,name.size())) return true;
      return false;
    };
    // Imports, aliases and macros can expose descendants through other
    // spellings. Once an ancestor is exposed, retain matching descendant names
    // conservatively. Parent ids precede their children, including reopenings.
    for (std::size_t id = 1; id < namespaces.size(); ++id) {
      auto& ns = namespaces[id];
      ns.exposed = namespaces[ns.parent].exposed;
      for (auto i : uses[ns.name]) if (exposes[i] && reaches(id,i)) ns.exposed = true;
      for (auto i : uses[ns.name]) if (reaches(id,i)) { ns.firstUse = i; break; }
    }
    // A live subtree provides all namespaces in its header. Prefer such a block
    // as the provider if it precedes every use; otherwise retain one empty
    // block. In particular, a reopened empty outer block cannot go if it alone
    // provides a referenced inner namespace.
    for (auto it = shells.rbegin(); it != shells.rend(); ++it)
      if (it->contents && it->parent != none) shells[it->parent].contents = true;
    auto keepShell = [&](std::size_t block) {
      for (; block != none && !shells[block].keep; block = shells[block].parent) shells[block].keep = true;
    };
    // Select descendant providers first, so their enclosing blocks can also
    // satisfy references to the outer namespace without another empty block.
    for (auto it = namespaces.rbegin(); it != namespaces.rend(); ++it) {
      const auto& ns = *it;
      if (ns.firstUse != n && !ns.blocks.empty()) {
        auto provider = ns.blocks.front();
        for (auto block : ns.blocks)
          if ((shells[block].contents || shells[block].keep) && shells[block].begin < ns.firstUse) {
            provider = block; break;
          }
        keepShell(provider);
      }
      // A plain reopening inherits lookup from the original inline declaration.
      if (!ns.inlineBlocks.empty() && std::any_of(ns.blocks.begin(),ns.blocks.end(),
          [&](auto block) { return shells[block].contents || shells[block].keep; }))
        keepShell(ns.inlineBlocks.front());
    }
    // Decide outermost first. Removing a subtree suppresses every nested shell,
    // regardless of unrelated occurrences of its inner names elsewhere.
    for (std::size_t i = 0; i < shells.size(); ++i) {
      const auto& s = shells[i];
      if (s.parent != none && !shells[s.parent].keep && !shells[s.parent].contents) continue;
      if (!s.keep && !s.contents)
        deletions.push_back({ts[s.begin].token->begin,ts[s.end].token->end});
    }
  }
  std::vector<EditRange> touching(EditRange range) const {
    std::vector<EditRange> result;
    auto it = std::lower_bound(deletions.begin(),deletions.end(),range.begin,[](auto d,auto p) { return d.end <= p; });
    for (; it != deletions.end() && it->begin < range.end; ++it) result.push_back(*it);
    return result;
  }
  bool deleted(EditRange range) const {
    const auto ds = touching(range); return !ds.empty() && contains(ds.front(),range);
  }
  bool marker(const LogicalLine& l) const {
    return std::binary_search(markers.begin(),markers.end(),EditRange{l.begin,l.end},[](auto a,auto b) { return a.begin < b.begin; });
  }
};
std::string newlines(std::string_view s) {
  std::string out;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\r' && i+1 < s.size() && s[i+1] == '\n') out += s[i];
    if (s[i] == '\n') out += s[i];
  }
  return out;
}
bool switchDirective(const LogicalLine& l) {
  if (!l.directive) return false;
  const auto& n = l.directive->name;
  return n == "if" || n == "ifdef" || n == "ifndef" || n == "elif" || n == "elifdef" || n == "elifndef" || n == "else" || n == "endif";
}
struct InvariantToken {
  std::string text;
  TokenKind kind;
  SourceLocation at;
  int boundary = 0;
  bool operator==(const InvariantToken&) const = default;
};
std::vector<InvariantToken> intended(const Input& in) {
  std::vector<InvariantToken> out;
  for (const auto& l : in.scan.lines) if (!in.marker(l) && !in.deleted({l.begin,l.end})) {
    // A directive's internal physical lines can contract when its comments
    // become spaces. Its logical start and complete token sequence must stay.
    const auto at = in.locations.at(l.firstLine-1);
    if (l.directive) out.push_back({"<directive-start>",TokenKind::punctuation,at,1});
    for (const auto& t : l.tokens) if (!in.deleted({t.begin,t.end}))
      out.push_back({t.text,t.kind,l.directive ? at : in.locations.at(t.line-1),0});
    if (l.directive) out.push_back({"<directive-end>",TokenKind::punctuation,at,2});
  }
  return out;
}
} // namespace
std::optional<std::size_t> RenderedSource::mapOffset(std::size_t p) const {
  auto it = std::upper_bound(copied.begin(),copied.end(),p,[](auto offset,const auto& s) { return offset < s.input.begin; });
  if (it == copied.begin()) return {};
  --it;
  if (p >= it->input.end) return {};
  return it->output + p - it->input.begin;
}
Result<RenderedSource> renderSource(const PruningModel& m, const PruningResult* retention, bool strip) {
  try {
    Input in(m,retention);
    RenderedSource out; out.provenance = m; out.trimming = in.trimming;
    auto& p = out.provenance;
    p.text.clear(); p.lineMap.clear(); p.generatedMarkers.clear(); p.sourceDirectives.clear();
    p.keepOffsets.clear(); p.annotations.clear(); p.expansions.clear();
    SourceLocation current{"",1};
    auto append = [&](std::string_view bytes, std::optional<SourceLocation> at) {
      if (bytes.size() > 128 * 1024 * 1024 - p.text.size()) throw Error{"rendered output size limit exceeded"};
      for (char c : bytes) {
        if (p.text.empty() || p.text.back() == '\n') p.lineMap.push_back(at);
        p.text += c;
        if (c == '\n') { ++current.line; if (at) ++at->line; }
      }
    };
    bool forceMarker = false;
    PresumedFile presumed; // Of the output: every marker is spelled here.
    std::size_t commentIndex = 0, conditionalDepth = 0;
    for (const auto& l : in.scan.lines) {
      const auto directive = l.directive ? l.directive->name : std::string{};
      const bool opening = directive == "if" || directive == "ifdef" || directive == "ifndef";
      const bool conditional = conditionalDepth != 0 || opening;
      if (opening) ++conditionalDepth;
      if (directive == "endif" && conditionalDepth) --conditionalDepth;
      if (in.marker(l)) {
        if (!retention) {
          // Re-spelled, not copied: its short form depends on this output.
          if (!p.text.empty() && p.text.back() != '\n') throw Error{"no safe marker boundary at source line " + std::to_string(l.firstLine)};
          current = in.markerAt.at(l.begin);
          const auto begin = p.text.size(); append(presumed.marker(current),{});
          p.generatedMarkers.push_back({begin,p.text.size()});
          forceMarker = false;
        } else forceMarker = true;
        continue;
      }
      const auto ds = in.touching({l.begin,l.end});
      std::vector<Comment> comments;
      while (commentIndex < in.scan.comments.size() && in.scan.comments[commentIndex].end <= l.begin) ++commentIndex;
      for (auto i = commentIndex; i < in.scan.comments.size() && in.scan.comments[i].begin < l.end; ++i) comments.push_back(in.scan.comments[i]);
      if (!ds.empty() && std::all_of(l.tokens.begin(),l.tokens.end(),[&](const auto& t) { return in.deleted({t.begin,t.end}); }) &&
          std::all_of(comments.begin(),comments.end(),[&](auto c) { return in.deleted({c.begin,c.end}); })) continue;
      const auto at = in.locations.at(l.firstLine-1);
      if (forceMarker || current != at) {
        if (at.file.empty()) throw Error{"missing source location for rendered line " + std::to_string(l.firstLine)};
        if (!p.text.empty() && p.text.back() != '\n') throw Error{"no safe marker boundary at source line " + std::to_string(l.firstLine)};
        const auto begin = p.text.size(); append(presumed.marker(at),{});
        p.generatedMarkers.push_back({begin,p.text.size()}); current = at;
      }
      forceMarker = retention && switchDirective(l); // A marker in the previous branch may be skipped.
      if (l.directive) presumed.directive(l.directive->name);
      struct Edit { EditRange range; std::string replacement; };
      std::vector<Edit> edits;
      for (auto d : ds) {
        d = {std::max(d.begin,l.begin),std::min(d.end,l.end)};
        edits.push_back({d," " + newlines(std::string_view(m.text).substr(d.begin,d.end-d.begin))});
      }
      if (strip) for (auto c : comments) if (!in.deleted({c.begin,c.end})) {
        const auto nl = newlines(std::string_view(m.text).substr(c.begin,c.end-c.begin));
        // Do not create a directive in a phase-3 line that used to have code
        // before the comment. Keeping the comment is the specified fallback.
        // A #line after a directive in a skipped alternative is ignored. In
        // particular it cannot repair __LINE__ in a later #elif expression.
        // Keep physical lines in conditional directives instead.
        bool safe = !(l.directive && conditional && !nl.empty());
        if (!l.directive && !nl.empty()) for (const auto& t : l.tokens)
          if (t.begin >= c.end && t.text == "#") safe = false;
        if (safe) edits.push_back({{c.begin,c.end},l.directive || nl.empty() ? " " : nl});
      }
      std::sort(edits.begin(),edits.end(),[](const auto& a,const auto& b) { return a.range.begin < b.range.begin; });
      const auto outputBegin = p.text.size();
      auto physical = l.firstLine;
      auto copy = [&](std::size_t a,std::size_t b) {
        if (a == b) return;
        out.copied.push_back({{a,b},p.text.size()});
        // Preserve physical provenance within multi-line comments/raw strings.
        while (a < b) {
          auto end = m.text.find('\n',a); end = end == m.text.npos ? b : std::min(b,end+1);
          std::optional<SourceLocation> loc;
          if (physical <= m.lineMap.size()) loc = m.lineMap[physical-1];
          append(std::string_view(m.text).substr(a,end-a),loc);
          if (m.text[end-1] == '\n') ++physical;
          a = end;
        }
      };
      auto begin = l.begin;
      for (const auto& edit : edits) {
        copy(begin,edit.range.begin);
        append(edit.replacement,in.locations.at(physical-1));
        physical += std::count(m.text.begin()+edit.range.begin,m.text.begin()+edit.range.end,'\n');
        begin = edit.range.end;
      }
      copy(begin,l.end);
      if (l.directive) p.sourceDirectives.push_back({outputBegin,p.text.size()});
    }
    auto ranges = [&](const std::vector<EditRange>& input) {
      std::vector<EditRange> mapped;
      for (auto r : input) {
        auto it = std::lower_bound(out.copied.begin(),out.copied.end(),r.begin,[](const auto& s,auto pos) { return s.input.end <= pos; });
        for (; it != out.copied.end() && it->input.begin < r.end; ++it) {
          const auto a = std::max(r.begin,it->input.begin), b = std::min(r.end,it->input.end);
          mapped.push_back({it->output+a-it->input.begin,it->output+b-it->input.begin});
        }
      }
      return mapped;
    };
    for (auto& f : p.families) f.ranges = ranges(f.ranges);
    for (auto offset : m.keepOffsets) if (auto mapped = out.mapOffset(offset)) p.keepOffsets.push_back(*mapped);
    for (auto a : m.annotations) if (auto mapped = out.mapOffset(a.offset)) { a.offset = *mapped; p.annotations.push_back(a); }
    for (auto e : m.expansions) if (auto mapped = out.mapOffset(e.offset)) { e.offset = *mapped; p.expansions.push_back(std::move(e)); }
    p.positionUses.clear();
    for (auto u : m.positionUses) if (auto mapped = out.mapOffset(u.offset)) { u.offset = *mapped; p.positionUses.push_back(std::move(u)); }
    return out;
  } catch (const Error& e) { return e; }
  catch (const std::exception& e) { return Error{std::string("render: ")+e.what()}; }
}
Result<bool> renderingInvariant(const PruningModel& m, const PruningResult* r, std::string_view actual) {
  try {
    const auto expected = intended(Input(m,r));
    PruningModel rendered; rendered.text = actual;
    // Actual markers are recognized lexically; no renderer-supplied metadata
    // can hide a malformed marker, glued token or a lost directive boundary.
    std::size_t lines = 0; for (char c : actual) if (c == '\n') ++lines;
    if (!actual.empty() && actual.back() != '\n') ++lines;
    rendered.lineMap.resize(lines);
    const auto observed = intended(Input(rendered,nullptr));
    std::size_t i = 0;
    while (i < expected.size() && i < observed.size() && expected[i] == observed[i]) ++i;
    if (i == expected.size() && i == observed.size()) return true;
    auto show = [i](const auto& tokens) {
      if (i == tokens.size()) return std::string("<end>");
      const auto& t = tokens[i]; return "'" + t.text + "' at " + t.at.file + ':' + std::to_string(t.at.line);
    };
    return Error{"rendering invariant at token " + std::to_string(i) + ": expected " + show(expected) + "; got " + show(observed)};
  } catch (const Error& e) { return e; }
  catch (const std::exception& e) { return Error{std::string("rendering invariant: ")+e.what()}; }
}
namespace {
std::string locate(const PruningModel& m, std::size_t offset) {
  const auto line = static_cast<std::size_t>(std::count(m.text.begin(),m.text.begin() + std::min(offset,m.text.size()),'\n'));
  if (line < m.lineMap.size() && m.lineMap[line]) return m.lineMap[line]->file + ':' + std::to_string(m.lineMap[line]->line);
  return "<generated>:" + std::to_string(line + 1);
}
bool lineDirective(const LogicalLine& l) {
  // #line, and GNU linemarkers (# 12 "f"), which the scanner names by number.
  return l.directive && (l.directive->name == "line" ||
      (!l.directive->name.empty() && std::isdigit(static_cast<unsigned char>(l.directive->name.front()))));
}
std::vector<InvariantToken> unmarkedTokens(std::string_view text, bool marked) {
  auto scan = take(scanDirectives(text));
  std::vector<InvariantToken> out;
  for (const auto& l : scan.lines) {
    if (lineDirective(l)) {
      if (!marked) throw Error{"line directive in unmarked text at line " + std::to_string(l.firstLine)};
      continue;
    }
    if (l.directive) out.push_back({"<directive-start>",TokenKind::punctuation,{},1});
    for (const auto& t : l.tokens) out.push_back({t.text,t.kind,{},0});
    if (l.directive) out.push_back({"<directive-end>",TokenKind::punctuation,{},2});
  }
  return out;
}
} // namespace
std::vector<std::string> lineMarkerObservations(const PruningModel& m, const std::vector<std::string>& argv) {
  std::vector<std::string> out;
  auto add = [&](std::string why) { if (std::find(out.begin(),out.end(),why) == out.end()) out.push_back(std::move(why)); };
  // The worker records every observation (rules 1-5 of the review). Position
  // macros are checked here again from the expansion facts as a second line.
  for (const auto& u : m.positionUses) add(u.what + " at " + locate(m,u.offset));
  for (const auto& e : m.expansions)
    if (e.name == "__LINE__" || e.name == "__FILE__" || e.name == "__FILE_NAME__" || e.name == "__BASE_FILE__" || e.name == "__INCLUDE_LEVEL__")
      add(e.name + " expanded at " + locate(m,e.offset));
  // A warning that depends on line layout must not decide compilation.
  for (const auto& word : argv) if (word.starts_with("-Werror")) add(word + " makes warnings errors");
  auto scan = scanDirectives(m.text);
  if (!scan.ok()) { add("directive scan failure: " + scan.error().message); return out; }
  for (const auto& l : scan.value().lines) if (l.directive && l.directive->name == "pragma") {
    const auto& t = l.directive->tokens;
    if (t.size() >= 3 && (t[0].text == "GCC" || t[0].text == "clang") && t[1].text == "diagnostic" && t[2].text == "error")
      add("diagnostic error pragma at " + locate(m,l.begin));
  }
  return out;
}
Result<std::string> withoutLineMarkers(const RenderedSource& rendered) {
  const auto& text = rendered.provenance.text;
  auto markers = rendered.provenance.generatedMarkers;
  std::sort(markers.begin(),markers.end(),[](auto a, auto b) { return a.begin < b.begin; });
  std::string out; out.reserve(text.size());
  std::size_t at = 0;
  for (auto r : markers) {
    if (r.begin < at || r.end <= r.begin || r.end > text.size() || (r.begin && text[r.begin-1] != '\n') ||
        text[r.end-1] != '\n' || !std::string_view(text).substr(r.begin,r.end-r.begin).starts_with("#line "))
      return Error{"generated marker is not a whole #line line at byte " + std::to_string(r.begin)};
    out.append(text,at,r.begin-at); at = r.end;
  }
  out.append(text,at,text.size()-at);
  return out;
}
Result<bool> unmarkedInvariant(std::string_view marked, std::string_view unmarked) {
  try {
    const auto expected = unmarkedTokens(marked,true), observed = unmarkedTokens(unmarked,false);
    std::size_t i = 0;
    while (i < expected.size() && i < observed.size() && expected[i] == observed[i]) ++i;
    if (i == expected.size() && i == observed.size()) return true;
    auto show = [i](const auto& tokens) { return i == tokens.size() ? std::string("<end>") : "'" + tokens[i].text + "'"; };
    return Error{"unmarked invariant at token " + std::to_string(i) + ": expected " + show(expected) + "; got " + show(observed)};
  } catch (const Error& e) { return e; }
  catch (const std::exception& e) { return Error{std::string("unmarked invariant: ")+e.what()}; }
}
Result<std::string> compactWhitespace(std::string_view text) {
  try {
    const auto scan = take(scanDirectives(text));
    std::vector<bool> protectedBytes(text.size(),false);
    auto protect = [&](std::size_t begin, std::size_t end) {
      std::fill(protectedBytes.begin()+begin,protectedBytes.begin()+end,true);
    };
    for (const auto& c : scan.comments) protect(c.begin,c.end);
    for (const auto& l : scan.lines) {
      for (const auto& t : l.tokens) if (t.kind == TokenKind::literal) protect(t.begin,t.end);
      // Keep whole spliced logical lines, including whitespace before/after
      // the backslash (GCC accepts horizontal whitespace before the newline).
      // Trimming a continuation can glue tokens or alter macro stringizing.
      for (auto i = l.begin; i < l.end; ++i) if (text[i] == '\\') {
        auto end = i+1;
        while (end < l.end && (text[end] == ' ' || text[end] == '\t' || text[end] == '\r' || text[end] == '\v' || text[end] == '\f')) ++end;
        if (end < l.end && text[end] == '\n') { protect(l.begin,l.end); break; }
      }
    }
    std::string out; out.reserve(text.size());
    auto horizontal = [](char c) { return c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\r'; };
    for (std::size_t begin = 0; begin < text.size();) {
      auto newline = text.find('\n',begin);
      const auto end = newline == text.npos ? text.size() : newline;
      auto first = begin, last = end;
      while (first < last && horizontal(text[first]) && !protectedBytes[first]) ++first;
      while (last > first && horizontal(text[last-1]) && !protectedBytes[last-1]) --last;
      // A protected newline can be an empty physical line inside a raw string
      // or a continued logical line. Preserve it even when it has no tokens.
      if (first != last || (end < text.size() && protectedBytes[end])) {
        out.append(text.substr(first,last-first));
        if (end < text.size()) out += '\n';
      }
      begin = end + (end < text.size());
    }
    return out;
  } catch (const Error& e) { return e; }
  catch (const std::exception& e) { return Error{std::string("compact whitespace: ")+e.what()}; }
}
} // namespace distill
