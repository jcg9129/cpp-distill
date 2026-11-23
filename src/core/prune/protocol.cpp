// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/prune/prune.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <type_traits>
namespace distill {
namespace {
// One protocol for the parent and worker, independent of LLVM. Every allocation
// is bounded by the byte cap and element cap; trailing/partial input is rejected.
struct Archive {
  std::stringstream stream;
  bool reading = false;
  std::string_view input;
  std::string path = "model";
  std::string invalidField, validationError;
  [[noreturn]] void fail(std::string message) {
    // tellg is unavailable after a failed extraction until failbit is cleared.
    stream.clear();
    const auto position = stream.tellg();
    const auto offset = position < 0 ? input.size() : std::min(input.size(),static_cast<std::size_t>(position));
    const auto prefix = input.substr(0,offset);
    const auto line = 1 + std::count(prefix.begin(),prefix.end(),'\n');
    throw Error{std::move(message) + " at field " + path + ", line " + std::to_string(line) + ", byte " + std::to_string(offset)};
  }
  template<class T> void field(const std::string& name, T& value) {
    const auto saved = path;
    path += (name.starts_with('[') ? "" : ".") + name;
    if (reading && path == invalidField) { stream >> std::ws; fail(validationError); }
    field(value);
    path = saved;
  }
  std::size_t elements = 0;
  template<class T> requires std::is_arithmetic_v<T> void field(T& value) {
    if (reading) { if (!(stream >> value)) fail("truncated or invalid pruning protocol number"); }
    else stream << value << ' ';
  }
  template<class T> requires std::is_enum_v<T> void field(T& value) {
    int n = static_cast<int>(value); field(n);
    constexpr int max = [] {
      if constexpr (std::is_same_v<T,FamilyKind>) return static_cast<int>(FamilyKind::instantiation);
      else if constexpr (std::is_same_v<T,MacroState>) return static_cast<int>(MacroState::unknown);
      else return static_cast<int>(FactOrigin::noProfile);
    }();
    if (n < 0 || n > max) fail("invalid pruning protocol enum");
    value = static_cast<T>(n);
  }
  void field(std::string& value) {
    if (reading) {
      stream >> std::ws;
      if (stream.peek() != '"' || !(stream >> std::quoted(value))) fail("invalid pruning protocol string");
    }
    else stream << std::quoted(value) << ' ';
  }
  template<class T> void field(std::vector<T>& value) {
    std::size_t size = value.size(); field("size",size);
    if (size > 1000000 || (elements += size) > 4000000) fail("pruning protocol element limit");
    if (reading) value.resize(size);
    for (std::size_t i = 0; i < value.size(); ++i) field("[" + std::to_string(i) + "]",value[i]);
  }
  template<class T> void field(std::optional<T>& value) {
    bool present = value.has_value(); field("present",present);
    if (reading) { if (present) value.emplace(); else value.reset(); }
    if (present) field("value",*value);
  }
  template<class T, class C> void field(std::set<T,C>& value) {
    std::vector<T> copy(value.begin(),value.end()); field(copy);
    if (reading) { value = {copy.begin(),copy.end()}; if (value.size() != copy.size()) fail("duplicate protocol set item"); }
  }
  template<class A, class B> void field(std::pair<A,B>& value) { field("first",value.first); field("second",value.second); }
  void field(MacroFacts& value) {
    std::vector<std::pair<std::string,MacroFact>> copy(value.begin(),value.end()); field(copy);
    if (reading) { value = {copy.begin(),copy.end()}; if (value.size() != copy.size()) fail("duplicate protocol macro"); }
  }
  void field(EditRange& x) { field("begin",x.begin); field("end",x.end); }
  void field(SourceLocation& x) { field("file",x.file); field("line",x.line); }
  void field(Macro& x) { field("functionLike",x.functionLike); field("parameters",x.parameters); field("variadic",x.variadic); field("replacement",x.replacement); }
  void field(MacroFact& x) { field("state",x.state); field("definition",x.definition); field("provenance",x.provenance); }
  void field(PruneMacroExpansion& x) {
    field("offset",x.offset); field("name",x.name); field("user",x.user); field("condition",x.condition); field("predefined",x.predefined);
    field("parse",x.parse); field("judge",x.judge); field("paste",x.paste); field("externalPasteOperand",x.externalPasteOperand); field("pasteOperandsKnown",x.pasteOperandsKnown);
    field("hashOperator",x.hashOperator);
  }
  void field(LiteralWidths& x) { field("intBits",x.intBits); field("longBits",x.longBits); field("longLongBits",x.longLongBits); }
  void field(PruneFamily& x) {
    field("kind",x.kind); field("label",x.label); field("names",x.names); field("callableNames",x.callableNames); field("dependentNames",x.dependentNames);
    field("ranges",x.ranges); field("edges",x.edges); field("associatedTypes",x.associatedTypes); field("original",x.original);
    field("pinned",x.pinned); field("neverRemoved",x.neverRemoved); field("root",x.root); field("nonMemberOperator",x.nonMemberOperator);
    field("instantiationEffect",x.instantiationEffect);
    field("boundedSystemHooks",x.boundedSystemHooks);
    field("rule1Kind",x.rule1Kind); field("rule1Constructor",x.rule1Constructor); field("rule1Record",x.rule1Record); field("rule1Declaration",x.rule1Declaration); field("potentialEdges",x.potentialEdges);
    field("memberOf",x.memberOf); field("memberName",x.memberName); field("memberBlocker",x.memberBlocker);
    field("namespaceUsing",x.namespaceUsing); field("externCDefinition",x.externCDefinition); field("systemSpecialization",x.systemSpecialization);
  }
  void field(PositionUse& x) { field("offset",x.offset); field("what",x.what); }
  void field(KeepAnnotation& x) { field("offset",x.offset); field("family",x.family); }
  void field(SystemEntity& x) { field("label",x.label); field("userEdges",x.userEdges); field("systemEdges",x.systemEdges); }
  void field(SystemUse& x) { field("family",x.family); field("entity",x.entity); }
  void field(PruningModel& x) {
    field("text",x.text); field("dialect",x.dialect); field("families",x.families); field("unions",x.unions); field("generatedMarkers",x.generatedMarkers);
    field("sourceDirectives",x.sourceDirectives); field("lineMap",x.lineMap); field("annotations",x.annotations); field("keepOffsets",x.keepOffsets); field("keepNames",x.keepNames); field("expansions",x.expansions);
    field("systemEntities",x.systemEntities); field("systemUses",x.systemUses); field("includes",x.includes); field("disabledReasons",x.disabledReasons);
    field("untranslatedOptions",x.untranslatedOptions); field("parseArgv",x.parseArgv); field("judgeInitial",x.judgeInitial); field("parseWidths",x.parseWidths); field("judgeProfile",x.judgeProfile);
    field("judgeComplete",x.judgeComplete); field("unknownInUserCode",x.unknownInUserCode); field("positionUses",x.positionUses);
    field("assumeUnreferencedOverloads",x.assumeUnreferencedOverloads);
    field("assumeInstantiationEffects",x.assumeInstantiationEffects);
    field("assumeLibrarySpelledMembers",x.assumeLibrarySpelledMembers);
    field("libraryMemberNamesComputed",x.libraryMemberNamesComputed); field("memberDisabledReasons",x.memberDisabledReasons);
    field("libraryMemberNames",x.libraryMemberNames);
  }
};
}
Result<std::string> serializePruningModel(const PruningModel& m) {
  auto v = validatePruningModel(m); if (!v.ok()) return v.error();
  try {
    Archive a; a.stream << "DISTILL-PRUNE-8 "; auto copy = m; a.field(copy);
    auto s = a.stream.str(); if (s.size() > 256 * 1024 * 1024) return Error{"pruning protocol byte limit"};
    return s;
  } catch (const Error& e) { return e; }
}
Result<PruningModel> parsePruningModel(std::string_view text) {
  if (text.size() > 256 * 1024 * 1024) return Error{"pruning protocol byte limit at field model, line 1, byte 0"};
  try {
    Archive a; a.reading = true; a.input = text; a.stream.str(std::string(text)); std::string version; a.stream >> version;
    if (version != "DISTILL-PRUNE-8") { a.path = "version"; a.fail("invalid pruning protocol version"); }
    PruningModel m; a.field(m); a.stream >> std::ws;
    if (!a.stream.eof()) { a.path = "trailer"; a.fail("trailing pruning protocol bytes"); }
    std::string invalidField;
    auto v = validatePruningModel(m,&invalidField);
    if (!v.ok()) {
      // Locate semantic failures without retaining a per-element offset map.
      // Release the decoded model, then replay only up to the invalid field.
      m = {};
      a.invalidField = std::move(invalidField);
      a.validationError = "invalid pruning protocol model: " + v.error().message;
      a.elements = 0; a.stream.clear(); a.stream.seekg(0); a.stream >> version;
      a.field(m);
      a.fail(a.validationError); // Defensive fallback if a field path drifts.
    }
    return m;
  } catch (const Error& e) { return e; }
}
} // namespace distill
