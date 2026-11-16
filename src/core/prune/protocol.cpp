// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/prune/prune.h"
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
  std::size_t elements = 0;
  template<class T> requires std::is_arithmetic_v<T> void field(T& value) {
    if (reading) { if (!(stream >> value)) throw Error{"truncated pruning protocol"}; }
    else stream << value << ' ';
  }
  template<class T> requires std::is_enum_v<T> void field(T& value) {
    int n = static_cast<int>(value); field(n);
    if (n < 0 || n > 64) throw Error{"invalid pruning protocol enum"};
    value = static_cast<T>(n);
  }
  void field(std::string& value) {
    if (reading) { if (!(stream >> std::quoted(value))) throw Error{"invalid pruning protocol string"}; }
    else stream << std::quoted(value) << ' ';
  }
  template<class T> void field(std::vector<T>& value) {
    std::size_t size = value.size(); field(size);
    if (size > 1000000 || (elements += size) > 4000000) throw Error{"pruning protocol element limit"};
    if (reading) value.resize(size);
    for (auto& x : value) field(x);
  }
  template<class T> void field(std::optional<T>& value) {
    bool present = value.has_value(); field(present);
    if (reading) { if (present) value.emplace(); else value.reset(); }
    if (present) field(*value);
  }
  template<class T, class C> void field(std::set<T,C>& value) {
    std::vector<T> copy(value.begin(),value.end()); field(copy);
    if (reading) { value = {copy.begin(),copy.end()}; if (value.size() != copy.size()) throw Error{"duplicate protocol set item"}; }
  }
  template<class A, class B> void field(std::pair<A,B>& value) { field(value.first); field(value.second); }
  void field(MacroFacts& value) {
    std::vector<std::pair<std::string,MacroFact>> copy(value.begin(),value.end()); field(copy);
    if (reading) { value = {copy.begin(),copy.end()}; if (value.size() != copy.size()) throw Error{"duplicate protocol macro"}; }
  }
  void field(EditRange& x) { field(x.begin); field(x.end); }
  void field(SourceLocation& x) { field(x.file); field(x.line); }
  void field(Macro& x) { field(x.functionLike); field(x.parameters); field(x.variadic); field(x.replacement); }
  void field(MacroFact& x) { field(x.state); field(x.definition); field(x.provenance); }
  void field(PruneMacroExpansion& x) {
    field(x.offset); field(x.name); field(x.user); field(x.condition); field(x.predefined);
    field(x.parse); field(x.judge); field(x.paste); field(x.externalPasteOperand); field(x.pasteOperandsKnown);
  }
  void field(PruneFamily& x) {
    field(x.kind); field(x.label); field(x.names); field(x.callableNames); field(x.dependentNames);
    field(x.ranges); field(x.edges); field(x.associatedTypes); field(x.original);
    field(x.pinned); field(x.neverRemoved); field(x.root); field(x.nonMemberOperator);
    field(x.namespaceUsing); field(x.externCDefinition); field(x.systemSpecialization);
  }
  void field(KeepAnnotation& x) { field(x.offset); field(x.family); }
  void field(SystemEntity& x) { field(x.label); field(x.userEdges); field(x.systemEdges); }
  void field(SystemUse& x) { field(x.family); field(x.entity); }
  void field(PruningModel& x) {
    field(x.text); field(x.dialect); field(x.families); field(x.unions); field(x.generatedMarkers);
    field(x.sourceDirectives); field(x.lineMap); field(x.annotations); field(x.keepOffsets); field(x.keepNames); field(x.expansions);
    field(x.systemEntities); field(x.systemUses); field(x.includes); field(x.disabledReasons);
    field(x.untranslatedOptions); field(x.parseArgv); field(x.judgeInitial); field(x.judgeProfile);
    field(x.judgeComplete); field(x.unknownInUserCode);
  }
};
}
Result<std::string> serializePruningModel(const PruningModel& m) {
  auto v = validatePruningModel(m); if (!v.ok()) return v.error();
  try {
    Archive a; a.stream << "DISTILL-PRUNE-1 "; auto copy = m; a.field(copy);
    auto s = a.stream.str(); if (s.size() > 256 * 1024 * 1024) return Error{"pruning protocol byte limit"};
    return s;
  } catch (const Error& e) { return e; }
}
Result<PruningModel> parsePruningModel(std::string_view text) {
  if (text.size() > 256 * 1024 * 1024) return Error{"pruning protocol byte limit"};
  try {
    Archive a; a.reading = true; a.stream.str(std::string(text)); std::string version; a.stream >> version;
    if (version != "DISTILL-PRUNE-1") return Error{"invalid pruning protocol version"};
    PruningModel m; a.field(m); a.stream >> std::ws;
    if (!a.stream.eof()) return Error{"trailing pruning protocol bytes"};
    auto v = validatePruningModel(m); if (!v.ok()) return v.error();
    return m;
  } catch (const Error& e) { return e; }
}
} // namespace distill
