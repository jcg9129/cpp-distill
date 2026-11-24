// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/render/minify.h"
#include "core/names/adl.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace distill {
namespace {
template<class T> T take(Result<T> r) { if (!r.ok()) throw r.error(); return std::move(r.value()); }
using Names = std::set<std::string,std::less<>>;
const Names& keywords() {
  static const Names names = [] {
    Names out; std::istringstream in(
      "alignas alignof and and_eq asm atomic_cancel atomic_commit atomic_noexcept auto bitand bitor bool break "
      "case catch char char8_t char16_t char32_t class compl concept const consteval constexpr constinit const_cast "
      "continue co_await co_return co_yield decltype default delete do double dynamic_cast else enum explicit export "
      "extern false float for friend goto if inline int long mutable namespace new noexcept not not_eq nullptr "
      "operator or or_eq private protected public reflexpr register reinterpret_cast requires return short signed "
      "sizeof static static_assert static_cast struct switch synchronized template this thread_local throw true try "
      "typedef typeid typename union unsigned using virtual void volatile wchar_t while xor xor_eq final override "
      "import module typeof typeof_unqual restrict contract_assert transaction_safe transaction_safe_dynamic defined __has_include __has_include_next "
      "__has_cpp_attribute __has_attribute __has_builtin __has_feature __has_extension __VA_ARGS__ __VA_OPT__ "
      "define undef include include_next ifdef ifndef elif elifdef elifndef endif pragma error warning line "
      "main _Pragma");
    for (std::string s; in >> s;) out.insert(s);
    return out;
  }();
  return names;
}
bool start(unsigned char c) { return std::isalpha(c) || c == '_' || c >= 128; }
bool rest(unsigned char c) { return start(c) || std::isdigit(c); }
void words(std::string_view text, Names& out) {
  for (std::size_t i = 0; i < text.size();) {
    if (!start(text[i])) { ++i; continue; }
    const auto a = i++;
    while (i < text.size() && rest(text[i])) ++i;
    out.emplace(text.substr(a,i-a));
  }
}
void literalNames(std::string_view text, Names& out) {
  words(text,out);
  // Also protect names encoded by octal/hex/universal escapes. Decoding is only
  // an exclusion overapproximation; emitted literal bytes always stay exact.
  std::string decoded;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '\\' || i+1 == text.size()) { decoded += text[i]; continue; }
    auto j = i+1; unsigned value = 0, digits = 0, base = 8, limit = 3;
    if (text[j] == 'x' || text[j] == 'u' || text[j] == 'U') {
      base = 16; limit = text[j] == 'u' ? 4 : text[j] == 'U' ? 8 : 1000000; ++j;
    }
    while (j < text.size() && digits < limit) {
      const auto c = static_cast<unsigned char>(text[j]);
      const unsigned n = c >= '0' && c <= '9' ? c-'0' : c >= 'a' && c <= 'f' ? c-'a'+10 : c >= 'A' && c <= 'F' ? c-'A'+10 : 99;
      if (n >= base) break;
      value = value*base+n; ++j; ++digits;
    }
    if (digits) { decoded += value < 128 ? static_cast<char>(value) : ' '; i = j-1; }
    else { decoded += ' '; ++i; }
  }
  words(decoded,out);
}
std::vector<Token> tokens(const ScanResult& s) {
  std::vector<Token> out;
  for (const auto& l : s.lines) out.insert(out.end(),l.tokens.begin(),l.tokens.end());
  return out;
}
std::string nameAt(std::size_t n) {
  constexpr std::string_view first = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  constexpr std::string_view more = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  std::size_t width = 1, count = first.size();
  while (n >= count) { n -= count; ++width; count *= more.size(); }
  std::string out(width,'a');
  for (auto i = width; i > 1; --i) { out[i-1] = more[n%more.size()]; n /= more.size(); }
  out[0] = first[n]; return out;
}
bool separate(const Token& a, const Token& b) {
  // Header-name recognition uses the surrounding directive. A literal suffix
  // is intentionally kept adjacent by the caller.
  if (b.kind == TokenKind::header) return false; // #include/has_include context is retained.
  if ((a.kind == TokenKind::literal && b.kind == TokenKind::identifier) ||
      (a.text == "." && b.text == ".") || (a.text == "<" && b.text == "::")) return true;
  const auto s = scanDirectives(a.text+b.text);
  if (!s.ok() || !s.value().comments.empty()) return true;
  const auto t = tokens(s.value());
  return t.size() != 2 || t[0].kind != a.kind || t[1].kind != b.kind || t[0].text != a.text || t[1].text != b.text;
}
struct Signature {
  TokenKind kind; std::string text; std::size_t directive;
  bool operator==(const Signature&) const = default;
};
std::vector<Signature> signature(std::string_view text, const RenameMap& map) {
  const auto scan = take(scanDirectives(text));
  std::vector<Signature> out; std::size_t directive = 0;
  const Token* previous = nullptr;
  for (const auto& l : scan.lines) {
    if (l.directive) { ++directive; previous = nullptr; }
    for (const auto& t : l.tokens) {
      if (previous && previous->kind == TokenKind::literal && t.kind == TokenKind::identifier)
        out.push_back({TokenKind::punctuation,previous->end == t.begin ? "<literal-suffix>" : "<literal-gap>",l.directive ? directive : 0});
      previous = &t;
      auto spelling = t.kind == TokenKind::literal ? std::string(text.substr(t.begin,t.end-t.begin)) : t.text;
      if (t.kind == TokenKind::identifier) if (const auto i = map.find(spelling); i != map.end()) spelling = i->second;
      out.push_back({t.kind,std::move(spelling),l.directive ? directive : 0});
    }
    if (l.directive) {
      previous = nullptr;
      // A sentinel preserves ends even for empty directives.
      out.push_back({TokenKind::punctuation,"<directive-end>",directive});
      const auto& t = l.tokens;
      if (l.directive->name == "define" && t.size() > 3 && t[3].text == "(")
        out.push_back({TokenKind::punctuation,t[2].end == t[3].begin ? "<function-macro>" : "<object-macro>",directive});
    }
  }
  return out;
}
} // namespace
Result<bool> minifyInvariant(std::string_view compact, std::string_view candidate, const RenameMap& map) {
  try {
    const auto original = take(scanDirectives(compact));
    Names originals, renamed;
    for (const auto& t : original.identifiers) originals.insert(t.text);
    for (const auto& name : originals) {
      const auto i = map.find(name);
      if (!renamed.insert(i == map.end() ? name : i->second).second) return Error{"minify rename collision"};
    }
    const auto expected = signature(compact,map), actual = signature(candidate,{});
    if (expected != actual) return Error{"minify token/directive invariant failed"};
    return true;
  } catch (const Error& e) { return e; }
}
Result<MinifiedSource> minifySource(std::string_view compact, const PruningModel& model) {
  try {
    MinifiedSource result;
    const auto scan = take(scanDirectives(compact)); const auto all = tokens(scan);
    Names excluded = keywords();
    excluded.insert(model.minifyExcludedNames.begin(),model.minifyExcludedNames.end());
    excluded.insert(model.libraryMemberNames.begin(),model.libraryMemberNames.end());
    for (const auto& row : embeddedAdlRegistry().entries) excluded.insert(row.name);
    Names sensitive;
    for (const auto& e : model.expansions) if (e.hashOperator) sensitive.insert(e.name);
    result.namesKept = model.minifyNamesRefused;
    if (!model.assumeLibrarySpelledNames) result.namesKept = "A15 library-spelled-names disabled (T1 only)";
    else if (!model.libraryMemberNamesComputed) return Error{"library-spelled identifier set was not computed"};
    static const Names observable = {"typeid","__func__","__FUNCTION__","__PRETTY_FUNCTION__","source_location","__builtin_FUNCTION"};
    for (const auto& [name,fact] : model.judgeInitial) {
      excluded.insert(name);
      if (fact.definition) for (const auto& p : fact.definition->parameters) excluded.insert(p);
    }
    unsigned attribute = 0, gnu = 0; bool pendingGnu = false;
    for (std::size_t i = 0; i < all.size(); ++i) {
      const auto& t = all[i];
      if (observable.contains(t.text)) result.namesKept = "observable names: " + t.text;
      if (t.kind == TokenKind::literal) literalNames(t.text,excluded);
      if (t.kind == TokenKind::number) {
        // Exponents and hex digits can precede a suffix; protect every possible
        // identifier tail, as the name-retention scanner does.
        for (std::size_t j = 1; j < t.text.size(); ++j) if (start(t.text[j])) excluded.insert(t.text.substr(j));
      }
      if (i && t.kind == TokenKind::identifier && all[i-1].kind == TokenKind::literal &&
          (all[i-1].end == t.begin || (i > 1 && all[i-2].text == "operator"))) excluded.insert(t.text);
      if (t.text == "[" && i+1 < all.size() && all[i+1].text == "[") ++attribute;
      if (t.text == "__attribute__" || t.text == "__declspec") pendingGnu = true;
      else if (pendingGnu) { pendingGnu = false; if (t.text == "(") gnu = 1; }
      else if (gnu && t.text == "(") ++gnu;
      else if (gnu && t.text == ")") --gnu;
      if (attribute || gnu) excluded.insert(t.text);
      if (t.text == "]" && i && all[i-1].text == "]" && attribute) --attribute;
      if (t.text.find("__") != t.text.npos || (t.text.size() > 1 && t.text[0] == '_' && std::isupper(static_cast<unsigned char>(t.text[1])))) excluded.insert(t.text);
      // asm goto labels and compiler-interpreted names are conservatively kept
      // throughout each asm construct (strings were handled above).
      if (t.text == "asm" || t.text == "__asm" || t.text == "__asm__" || t.text == "_Pragma") {
        for (auto j = i+1; j < all.size() && all[j].text != ";"; ++j) if (all[j].kind == TokenKind::identifier) excluded.insert(all[j].text);
      }
    }
    for (const auto& l : scan.lines) if (l.directive) {
      const auto& t = l.tokens;
      if (l.directive->name == "pragma") for (const auto& x : t) if (x.kind == TokenKind::identifier) excluded.insert(x.text);
      if ((l.directive->name == "define" || l.directive->name == "undef") && t.size() > 2) excluded.insert(t[2].text);
      if (l.directive->name == "define" && t.size() > 3) {
        if (t[3].text == "(" && t[2].end == t[3].begin) for (auto j = 4u; j < t.size() && t[j].text != ")"; ++j) excluded.insert(t[j].text);
        if (std::any_of(t.begin()+3,t.end(),[](const auto& x) { return x.text == "#" || x.text == "##" || x.text == "%:" || x.text == "%:%:"; })) {
          sensitive.insert(t[2].text);
          for (const auto& x : t) if (x.kind == TokenKind::identifier) excluded.insert(x.text);
        }
      }
    }
    // Protect the whole invocation, including forwarding macros identified by
    // the worker's hash provenance; all argument spellings and gaps stay.
    std::vector<EditRange> protectedRanges;
    for (std::size_t i = 0; i+1 < all.size(); ++i) if (sensitive.contains(all[i].text) && all[i+1].text == "(") {
      unsigned depth = 0;
      for (auto j = i+1; j < all.size(); ++j) {
        if (all[j].kind == TokenKind::identifier) excluded.insert(all[j].text);
        if (all[j].text == "(") ++depth;
        if (all[j].text == ")" && !--depth) { protectedRanges.push_back({all[i].begin,all[j].end}); break; }
      }
    }
    std::map<std::string,std::size_t,std::less<>> counts;
    for (const auto& t : all) if (t.kind == TokenKind::identifier && !excluded.contains(t.text)) ++counts[t.text];
    if (result.namesKept.empty()) {
      std::vector<std::pair<std::string,std::size_t>> ordered(counts.begin(),counts.end());
      std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
      auto blocked = excluded;
      for (const auto& t : all) if (t.kind == TokenKind::identifier && !counts.contains(t.text)) blocked.insert(t.text);
      std::size_t next = 0;
      for (const auto& [old,count] : ordered) {
        (void)count;
        auto name = nameAt(next++); while (blocked.contains(name)) name = nameAt(next++);
        blocked.insert(name);
        if (name != old) result.renames.emplace(old,std::move(name));
      }
    }
    // Rename and remove gaps in one pass. Macro replacement-list gaps can be
    // observed by later stringizing, so retain those, pragma gaps and splices.
    // Directive boundaries and literal bytes always come from the input.
    std::size_t end = 0; const Token* previous = nullptr;
    auto mapped = [&](Token token) {
      if (token.kind == TokenKind::identifier) if (const auto i = result.renames.find(token.text); i != result.renames.end()) token.text = i->second;
      return token;
    };
    std::map<std::pair<std::string,std::string>,bool> spaceCache;
    auto needsSpace = [&](const Token& left, const Token& right) {
      const auto key = std::pair{left.text,right.text};
      auto [i,fresh] = spaceCache.emplace(key,false);
      if (fresh) i->second = separate(left,right);
      return i->second;
    };
    for (const auto& l : scan.lines) {
      std::size_t body = compact.size(); bool objectParen = false;
      if (l.directive && l.directive->name == "define" && l.tokens.size() > 3) {
        const auto& ts = l.tokens; std::size_t index = 3;
        const bool function = ts[3].text == "(" && ts[2].end == ts[3].begin;
        objectParen = ts[3].text == "(" && !function;
        if (function) { while (index < ts.size() && ts[index].text != ")") ++index; ++index; }
        if (index < ts.size()) body = ts[index].begin;
      }
      for (const auto& t : l.tokens) {
        const auto gap = compact.substr(end,t.begin-end);
        const bool firstDirectiveToken = l.directive && &t == &l.tokens.front();
        bool protect = firstDirectiveToken || (l.directive && l.directive->name == "pragma") ||
            (previous && previous->begin >= body) || gap.find('\\') != gap.npos;
        const bool forceSpace = objectParen && &t == &l.tokens[3];
        // Keep comments should a caller supply bytes without --strip-comments.
        for (const auto& c : scan.comments) if (end <= c.begin && c.begin < t.begin) protect = true;
        for (auto r : protectedRanges) if (end >= r.begin && t.begin < r.end) protect = true;
        const auto current = mapped(t);
        const auto left = previous ? mapped(*previous) : current;
        // A rename can itself introduce a literal prefix even when the old
        // tokens had no gap (e.g. a macro replacement identifier before "x").
        const bool changedBoundary = previous && (left.text != previous->text || current.text != t.text);
        if (protect) {
          result.text += gap;
          if (gap.empty() && changedBoundary && needsSpace(left,current)) result.text += ' ';
        } else if (forceSpace || (previous && (!gap.empty() || changedBoundary) && needsSpace(left,current))) result.text += ' ';
        if (t.kind == TokenKind::identifier && result.renames.contains(t.text)) result.text += current.text;
        else result.text += compact.substr(t.begin,t.end-t.begin);
        end = t.end; previous = &t;
      }
      if (l.directive) { result.text += compact.substr(end,l.end-end); end = l.end; previous = nullptr; }
    }
    const auto tail = compact.substr(end);
    if (!std::all_of(tail.begin(),tail.end(),[](unsigned char c) { return std::isspace(c); })) result.text += tail;
    take(minifyInvariant(compact,result.text,result.renames));
    return result;
  } catch (const Error& e) { return e; }
  catch (const std::exception& e) { return Error{std::string("minify: ")+e.what()}; }
}
} // namespace distill
