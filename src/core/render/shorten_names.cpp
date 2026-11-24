// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/render/shorten_names.h"
#include "core/names/adl.h"
#include "core/names/names.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace distill {
namespace embedded { extern const std::string_view abbreviations; }
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
// Preserve underscores and split camelCase / PascalCase / acronym boundaries.
// Digits belong to their word, so uint32 and float64 are never abbreviated.
std::vector<std::string> parts(std::string_view name) {
  std::vector<std::string> out;
  const auto upper = [](char c) { return c >= 'A' && c <= 'Z'; };
  const auto lower = [](char c) { return c >= 'a' && c <= 'z'; };
  for (std::size_t begin = 0; begin < name.size();) {
    auto end = begin+1;
    if (name[begin] == '_') {
      while (end < name.size() && name[end] == '_') ++end;
    } else {
      while (end < name.size() && name[end] != '_') {
        if (upper(name[end]) && (!upper(name[end-1]) ||
            (end+1 < name.size() && lower(name[end+1])))) break;
        ++end;
      }
    }
    out.emplace_back(name.substr(begin,end-begin)); begin = end;
  }
  return out;
}
std::string abbreviate(const std::string& word) {
  if (word.size() < 4 || !std::all_of(word.begin(),word.end(),[](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
      })) return word;
  static const auto dictionary = [] {
    std::map<std::string,std::string,std::less<>> out;
    std::istringstream lines{std::string(embedded::abbreviations)};
    for (std::string line; std::getline(lines,line);) {
      if (line.empty() || line.front() == '#') continue;
      std::istringstream row(line); std::string original, shortWord;
      row >> original >> shortWord; out.emplace(original,shortWord);
    }
    return out;
  }();
  std::string lower = word;
  for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  std::string shortWord;
  if (const auto i = dictionary.find(lower); i != dictionary.end()) shortWord = i->second;
  else {
    shortWord += lower.front();
    for (std::size_t i = 1; i < lower.size() && shortWord.size() < 4; ++i)
      if (std::string_view("aeiou").find(lower[i]) == std::string_view::npos) shortWord += lower[i];
  }
  if (shortWord.size()+2 > word.size()) return word;
  if (std::all_of(word.begin(),word.end(),[](unsigned char c) { return std::isupper(c); })) {
    for (auto& c : shortWord) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  } else if (std::isupper(static_cast<unsigned char>(word.front())))
    shortWord.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(shortWord.front())));
  return shortWord;
}
std::string join(const std::vector<std::string>& words) {
  std::string out; for (const auto& word : words) out += word; return out;
}
std::string readableName(const std::string& name, const Names& blocked) {
  if (name.size() <= 4) return name;
  const auto original = parts(name);
  auto shortWords = original;
  for (auto& word : shortWords) word = abbreviate(word);
  const auto base = join(shortWords);
  if (base == name) return name;
  const auto available = [&](const std::string& candidate) {
    return candidate.size()+2 <= name.size() && !blocked.contains(candidate) && !isReserved(candidate);
  };
  if (available(base)) return base;
  // Left-to-right rounds restore prefixes one letter at a time. Restore a word
  // completely when the next prefix would save only one character.
  while (shortWords != original) {
    for (std::size_t i = 0; i < original.size(); ++i) if (shortWords[i] != original[i]) {
      auto length = shortWords[i].size()+1;
      if (length+2 > original[i].size()) length = original[i].size();
      shortWords[i] = original[i].substr(0,length);
      const auto candidate = join(shortWords);
      if (available(candidate)) return candidate;
    }
  }
  // Fully restoring the name does not shorten it. Suffix the recognizable base
  // instead, and retain the original if no suffix can still save two bytes.
  for (std::size_t digit = 1;; ++digit) {
    auto candidate = base+std::to_string(digit);
    if (candidate.size()+2 > name.size()) return name;
    if (available(candidate)) return candidate;
  }
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
      if (l.directive->name == "define" && t.size() > 3) {
        const bool function = t[3].text == "(" && t[2].end == t[3].begin;
        out.push_back({TokenKind::punctuation,function ? "<function-macro>" :
            t[2].end < t[3].begin ? "<object-macro>" : "<missing-object-separator>",directive});
      }
    }
  }
  return out;
}
} // namespace
Result<bool> shortenNamesInvariant(std::string_view compact, std::string_view candidate, const RenameMap& map) {
  try {
    const auto original = take(scanDirectives(compact));
    Names originals, renamed;
    for (const auto& t : original.identifiers) originals.insert(t.text);
    for (const auto& name : originals) {
      const auto i = map.find(name);
      if (!renamed.insert(i == map.end() ? name : i->second).second) return Error{"shorten-names rename collision"};
    }
    const auto expected = signature(compact,map), actual = signature(candidate,{});
    if (expected != actual) return Error{"shorten-names token/directive invariant failed"};
    return true;
  } catch (const Error& e) { return e; }
}
Result<ShortenedSource> shortenNamesSource(std::string_view compact, const PruningModel& model) {
  try {
    ShortenedSource result;
    const auto scan = take(scanDirectives(compact)); const auto all = tokens(scan);
    Names excluded = keywords();
    excluded.insert(shortenNamesCompilerNames().begin(),shortenNamesCompilerNames().end());
    excluded.insert(model.shorteningExcludedNames.begin(),model.shorteningExcludedNames.end());
    excluded.insert(model.libraryMemberNames.begin(),model.libraryMemberNames.end());
    for (const auto& row : embeddedAdlRegistry().entries) excluded.insert(row.name);
    Names sensitive;
    result.namesKept = model.shorteningRefused;
    if (!model.assumeLibrarySpelledNames) result.namesKept = "A15 library-spelled-names disabled";
    else if (!model.libraryMemberNamesComputed) return Error{"library-spelled identifier set was not computed"};
    static const Names observable = {"typeid","__func__","__FUNCTION__","__PRETTY_FUNCTION__","source_location","__builtin_FUNCTION"};
    for (const auto& [name,fact] : model.judgeInitial) {
      excluded.insert(name);
      if (fact.definition) for (const auto& p : fact.definition->parameters) excluded.insert(p);
    }
    unsigned attribute = 0, gnu = 0; bool pendingGnu = false;
    for (std::size_t i = 0; i < all.size(); ++i) {
      const auto& t = all[i];
      // Keep identifiers spanning physical splices, preserving every line break.
      if (t.kind == TokenKind::identifier && compact.substr(t.begin,t.end-t.begin) != t.text) excluded.insert(t.text);
      if (observable.contains(t.text)) result.namesKept = "observable names: " + t.text;
      if (t.kind == TokenKind::literal) literalNames(t.text,excluded);
      if (t.kind == TokenKind::number) {
        // Exponents and hex digits can precede a suffix; protect every possible
        // identifier tail, as the name-retention scanner does.
        for (std::size_t j = 1; j < t.text.size(); ++j) if (start(t.text[j])) excluded.insert(t.text.substr(j));
      }
      if (i && t.kind == TokenKind::identifier && all[i-1].kind == TokenKind::literal &&
          (all[i-1].end == t.begin || (i > 1 && all[i-2].text == "operator"))) excluded.insert(t.text);
      if (t.text == "__has_attribute" || t.text == "__has_cpp_attribute" || t.text == "__has_builtin" ||
          t.text == "__has_feature" || t.text == "__has_extension" || t.text == "__is_identifier") {
        unsigned depth = 0;
        for (auto j = i+1; j < all.size(); ++j) {
          if (all[j].text == "(") ++depth;
          if (!depth) break;
          if (all[j].kind == TokenKind::identifier) excluded.insert(all[j].text);
          if (all[j].text == ")" && !--depth) break;
        }
      }
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
    // The worker excludes actual stringizing/pasting arguments, including
    // invocations assembled from macro replacement lists. Raw scanning also
    // protects straightforward inactive invocations without changing any gaps.
    std::vector<EditRange> protectedRanges;
    // Also cover unexpanded/inactive source occurrences. Definitions are
    // already protected; a matching search may never cross a directive.
    std::vector<Token> code;
    auto protectCode = [&]() -> Result<bool> {
      for (std::size_t i = 0; i < code.size(); ++i) if (sensitive.contains(code[i].text)) {
        if (i+1 == code.size() || code[i+1].text != "(") continue;
        unsigned depth = 0; bool closed = false;
        for (auto j = i+1; j < code.size(); ++j) {
          if (code[j].text == "(") ++depth;
          if (code[j].text == ")" && !--depth) {
            protectedRanges.push_back({code[i].begin,code[j].end}); closed = true; break;
          }
        }
        if (!closed) protectedRanges.push_back({code[i].begin,code.back().end});
      }
      code.clear(); return true;
    };
    for (const auto& l : scan.lines) {
      if (l.directive) take(protectCode());
      else code.insert(code.end(),l.tokens.begin(),l.tokens.end());
    }
    take(protectCode());
    for (const auto& t : all) for (auto r : protectedRanges)
      if (r.begin <= t.begin && t.begin < r.end && t.kind == TokenKind::identifier) excluded.insert(t.text);
    std::map<std::string,std::size_t,std::less<>> counts;
    for (const auto& t : all) if (t.kind == TokenKind::identifier && !excluded.contains(t.text)) ++counts[t.text];
    if (result.namesKept.empty()) {
      std::vector<std::pair<std::string,std::size_t>> ordered(counts.begin(),counts.end());
      std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
      auto blocked = excluded;
      // Reserve every original spelling, even one scheduled for renaming.
      // Frequency then spelling determines which word gets a shared abbreviation.
      for (const auto& t : all) if (t.kind == TokenKind::identifier) blocked.insert(t.text);
      for (const auto& [old,count] : ordered) {
        (void)count;
        auto name = readableName(old,blocked);
        if (name != old) {
          blocked.insert(name);
          result.renames.emplace(old,std::move(name));
        }
      }
    }
    // Substitute identifiers simultaneously, preserving all compact layout bytes.
    std::size_t end = 0;
    for (const auto& t : all) {
      result.text += compact.substr(end,t.begin-end);
      if (t.kind == TokenKind::identifier && result.renames.contains(t.text)) result.text += result.renames.at(t.text);
      else result.text += compact.substr(t.begin,t.end-t.begin);
      end = t.end;
    }
    result.text += compact.substr(end);
    take(shortenNamesInvariant(compact,result.text,result.renames));
    return result;
  } catch (const Error& e) { return e; }
  catch (const std::exception& e) { return Error{std::string("shorten-names: ")+e.what()}; }
}
} // namespace distill
