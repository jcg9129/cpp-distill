// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/render/render.h"
#include "core/validate/preprocess.h"
#include <algorithm>
#include <cctype>

namespace distill {
namespace {
template<class T> T take(Result<T> r) { if (!r.ok()) throw r.error(); return std::move(r.value()); }
bool contains(EditRange a, EditRange b) { return a.begin <= b.begin && b.end <= a.end; }
struct Input {
  const PruningModel& model;
  ScanResult scan;
  std::vector<EditRange> deletions, markers;
  std::vector<SourceLocation> locations;
  Input(const PruningModel& m, const PruningResult* r) : model(m), scan(take(scanDirectives(m.text))) {
    take(validatePruningModel(m));
    if (r) {
      if (!r->disabledReasons.empty() || r->families.size() != m.families.size())
        throw Error{"rendering requires enabled matching retention"};
      for (FamilyId id = 0; id < m.families.size(); ++id) if (!r->families[id].kept)
        deletions.insert(deletions.end(),m.families[id].ranges.begin(),m.families[id].ranges.end());
    }
    std::sort(deletions.begin(),deletions.end(),[](auto a,auto b) { return a.begin < b.begin; });
    // Adjacent ownership partitions may together own a single syntactic token.
    std::vector<EditRange> joined;
    for (auto d : deletions) {
      if (!joined.empty() && joined.back().end == d.begin) joined.back().end = d.end;
      else joined.push_back(d);
    }
    deletions = std::move(joined);
    for (const auto& l : scan.lines) {
      const bool generated = l.directive && l.directive->name == "line" &&
          l.firstLine <= m.lineMap.size() && !m.lineMap[l.firstLine-1];
      if (generated) markers.push_back({l.begin,l.end});
      for (const auto& t : l.tokens) for (auto d : touching({t.begin,t.end}))
        if (!contains(d,{t.begin,t.end})) throw Error{"deletion splits token '" + t.text + "' at byte " + std::to_string(t.begin)};
      if (l.directive && !generated && !touching({l.begin,l.end}).empty())
        throw Error{"deletion touches source directive #" + l.directive->name + " at line " + std::to_string(l.firstLine)};
    }
    for (auto c : scan.comments) for (auto d : touching({c.begin,c.end}))
      if (!contains(d,{c.begin,c.end})) throw Error{"deletion splits comment at byte " + std::to_string(c.begin)};
    // Interpret only lexer-recognized markers; a marker-shaped raw-string line
    // is ordinary source. D2 provenance overrides the presumed coordinates.
    SourceLocation at{"flat.cpp",1};
    std::size_t physical = 1;
    for (std::size_t begin = 0; begin < m.text.size(); ++physical) {
      auto end = m.text.find('\n',begin); if (end == m.text.npos) end = m.text.size();
      locations.push_back(physical <= m.lineMap.size() && m.lineMap[physical-1] ? *m.lineMap[physical-1] : at);
      if (std::binary_search(markers.begin(),markers.end(),EditRange{begin,end + (end < m.text.size())},
          [](auto a,auto b) { return a.begin < b.begin; })) {
        if (!take(parseLineMarker(std::string_view(m.text).substr(begin,end-begin),at)))
          throw Error{"invalid generated marker at line " + std::to_string(physical)};
      } else ++at.line;
      begin = end + (end < m.text.size());
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
std::string markerText(const SourceLocation& at) {
  std::string escaped;
  for (char c : at.file) { if (c == '\\' || c == '"') escaped += '\\'; escaped += c; }
  return "#line " + std::to_string(at.line) + " \"" + escaped + "\"\n";
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
  for (const auto& l : in.scan.lines) if (!in.marker(l)) {
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
    RenderedSource out; out.provenance = m;
    auto& p = out.provenance;
    p.text.clear(); p.lineMap.clear(); p.generatedMarkers.clear(); p.sourceDirectives.clear();
    p.keepOffsets.clear(); p.annotations.clear(); p.expansions.clear();
    SourceLocation current{"flat.cpp",1};
    auto append = [&](std::string_view bytes, std::optional<SourceLocation> at) {
      if (bytes.size() > 128 * 1024 * 1024 - p.text.size()) throw Error{"rendered output size limit exceeded"};
      for (char c : bytes) {
        if (p.text.empty() || p.text.back() == '\n') p.lineMap.push_back(at);
        p.text += c;
        if (c == '\n') { ++current.line; if (at) ++at->line; }
      }
    };
    bool forceMarker = false;
    std::size_t commentIndex = 0, conditionalDepth = 0;
    for (const auto& l : in.scan.lines) {
      const auto directive = l.directive ? l.directive->name : std::string{};
      const bool opening = directive == "if" || directive == "ifdef" || directive == "ifndef";
      const bool conditional = conditionalDepth != 0 || opening;
      if (opening) ++conditionalDepth;
      if (directive == "endif" && conditionalDepth) --conditionalDepth;
      if (in.marker(l)) {
        if (!retention) {
          const auto begin = p.text.size();
          out.copied.push_back({{l.begin,l.end},begin});
          append(std::string_view(m.text).substr(l.begin,l.end-l.begin),{});
          p.generatedMarkers.push_back({begin,p.text.size()});
          auto text = std::string_view(m.text).substr(l.begin,l.end-l.begin);
          if (!text.empty() && text.back() == '\n') text.remove_suffix(1);
          take(parseLineMarker(text,current)); forceMarker = false;
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
        if (!p.text.empty() && p.text.back() != '\n') throw Error{"no safe marker boundary at source line " + std::to_string(l.firstLine)};
        const auto begin = p.text.size(); append(markerText(at),{});
        p.generatedMarkers.push_back({begin,p.text.size()}); current = at;
      }
      forceMarker = retention && switchDirective(l); // A marker in the previous branch may be skipped.
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
} // namespace distill
