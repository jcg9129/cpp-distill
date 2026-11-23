// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.h"
#include "core/prune/extract.h"
#include "core/names/adl.h"
#include "core/library/library.h"
#include "core/report/prune.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
using namespace distill;
namespace {
struct Model {
  PruningModel m;
  Model() { m.dialect = "gnu++20"; }
  FamilyId add(std::string name, std::string text, FamilyKind kind = FamilyKind::function) {
    auto begin = m.text.size(); m.text += text; m.text += '\n';
    PruneFamily f; f.label = name; f.kind = kind; f.names.insert(name);
    if (kind == FamilyKind::function) f.callableNames.insert(name);
    f.ranges.push_back({begin,m.text.size()}); m.families.push_back(f); return m.families.size()-1;
  }
  PruningResult run() { auto r = computeRetention(m); if (!r.ok()) throw std::runtime_error(r.error().message); return r.value(); }
};
MacroFact known(std::string s) { return {MacroState::known,Macro{false,{},false,std::move(s)},{FactOrigin::compilerBuiltin}}; }
}
TEST(prune_empty_and_unreachable_cycles) {
  Model b; CHECK(b.run().families.empty());
  b.add("a","int a(){return b();}"); b.add("b","int b(){return a();}");
  auto r = b.run(); CHECK(!r.families[0].kept); CHECK(!r.families[1].kept);
  CHECK(r.assumptions.contains("A6")); CHECK(r.assumptions.contains("A7")); CHECK(r.assumptions.contains("A11")); CHECK_EQ(r.keptBytes,0u); CHECK_EQ(r.removedBytes,b.m.text.size());
}
TEST(prune_roots_name_ast_association_fixpoint) {
  Model b; auto main = b.add("main","int main(){return invoke();}");
  auto invoke = b.add("invoke","int invoke(){return 0;}");
  auto record = b.add("Key","struct Key{};",FamilyKind::record);
  auto spec = b.add("hash","template<> struct hash<Key>{};",FamilyKind::specialization);
  auto dep = b.add("dep","int dep(){return 1;}"); b.add("unused","void unused(){}");
  b.m.families[invoke].edges = {record}; b.m.families[spec].associatedTypes = {record}; b.m.families[spec].edges = {dep};
  auto r = b.run(); for (auto i : {main,invoke,record,spec,dep}) CHECK(r.families[i].kept);
  CHECK(!r.families.back().kept); CHECK_EQ(r.families[dep].initialCause,main);
  CHECK_EQ(r.families[spec].reason,"associated with Key");
  CHECK_EQ(r.keptBytes+r.removedBytes,b.m.text.size());
  auto report = renderPruningReport(b.m,r); CHECK(report.ok());
  CHECK(report.value().find("pruning assumption: A6") != std::string::npos);
  CHECK(report.value().find("pruning assumption: A7\n") != std::string::npos);
  CHECK(report.value().find("pruning assumption: A11 instantiation-effects\n") != std::string::npos);
  CHECK(report.value().find("transitive bytes") != std::string::npos);
  CHECK(report.value().find("does not certify an artifact") != std::string::npos);
  std::size_t sum = 0; for (const auto& [reason,bytes] : r.transitiveBytes) { (void)reason; sum += bytes; } CHECK_EQ(sum,r.keptBytes);
}
TEST(prune_union_closure_without_merging_ranges) {
  Model b; auto a = b.add("a","void a(){}"); auto c = b.add("c","void c(){}"); auto d = b.add("d","void d(){}");
  b.m.unions = {{a,c},{d,c},{a,a}}; b.m.families[d].root = "explicit keep";
  auto r = b.run(); for (const auto& f : r.families) CHECK(f.kept);
  CHECK_EQ(b.m.families[a].ranges.size(),1u); CHECK_EQ(r.families[a].initialCause,d);
}
TEST(prune_a7_a11_apply_even_without_family_deletion) {
  Model b; b.add("main","int main(){}");
  const auto r = b.run(); CHECK(r.families[0].kept); CHECK_EQ(r.removedBytes,0u);
  CHECK(r.assumptions.contains("A7")); CHECK(r.assumptions.contains("A11"));
  CHECK(!r.assumptions.contains("A6"));
  CHECK(b.run().policy == "D3e");
}
TEST(prune_without_instantiation_effects_identity_and_report) {
  Model b; b.add("main","int main(){}"); b.add("dead","int dead(){return 0;}");
  const auto assumed = serializePruningModel(b.m).value();
  b.m.assumeInstantiationEffects = false;
  const auto encoded = serializePruningModel(b.m).value();
  CHECK(assumed != encoded); CHECK(encoded.starts_with("DISTILL-PRUNE-8 "));
  CHECK(!parsePruningModel(encoded).value().assumeInstantiationEffects);
  CHECK(parsePruningModel(assumed).value().assumeInstantiationEffects);
  CHECK(!parsePruningModel("DISTILL-PRUNE-2 ").ok());
  const auto r = b.run(); CHECK(r.disabledReasons.empty()); CHECK(!r.families[1].kept);
  CHECK(r.assumptions.contains("A7")); CHECK(!r.assumptions.contains("A11"));
  CHECK_EQ(r.policy,"D3e, without instantiation-effects");
  const auto report = renderPruningReport(b.m,r).value();
  CHECK(report.find("pruning assumption: A11") == std::string::npos);
  CHECK(report.find("pruning assumption disabled: A11 instantiation-effects (--no-assume=instantiation-effects)") != std::string::npos);
  test::TempDirectory temp; PruneWorkerOptions o; o.temporaryRoot = temp.path; o.executable = test::helper;
  o.runner = [&](const ProcessSpec&) -> Result<ProcessResult> {
    ProcessResult response; response.exitCode = 0; response.stdoutText = assumed; return response;
  };
  const auto mixed = extractPruning(b.m,o).value();
  CHECK(!mixed.assumeInstantiationEffects);
  CHECK(mixed.disabledReasons.back().starts_with("pruning worker changed immutable input"));
}

TEST(prune_unreferenced_overloads_worker_identity) {
  Model b; b.add("main","int main(){}");
  const auto enabled = serializePruningModel(b.m).value();
  b.m.assumeUnreferencedOverloads = false;
  const auto disabled = serializePruningModel(b.m).value(); CHECK(enabled != disabled);
  CHECK(!parsePruningModel(disabled).value().assumeUnreferencedOverloads);
  CHECK(parsePruningModel(enabled).value().assumeUnreferencedOverloads);
  CHECK(renderPruningReport(b.m,b.run()).value().find("pruning assumption disabled: unreferenced-overloads") != std::string::npos);
  test::TempDirectory temp; PruneWorkerOptions o; o.temporaryRoot = temp.path; o.executable = test::helper;
  o.runner = [&](const ProcessSpec&) -> Result<ProcessResult> {
    ProcessResult response; response.exitCode = 0; response.stdoutText = enabled; return response;
  };
  const auto mixed = extractPruning(b.m,o).value();
  CHECK(!mixed.assumeUnreferencedOverloads);
  CHECK(mixed.disabledReasons.back().starts_with("pruning worker changed immutable input"));
}

TEST(prune_library_spelled_members_identity_fallback_and_report) {
  Model b; const auto owner = b.add("S","struct S {};",FamilyKind::record);
  const auto member = b.add("unused","int unused(){return 0;}");
  b.m.families[member].memberOf = owner; b.m.families[member].memberName = "unused";
  b.m.families[member].edges = {owner};
  b.add("main","int main(){S s;}"); const auto dead = b.add("dead","int dead(){return 0;}");
  // An incomplete inventory must keep members while still pruning whole families.
  auto r = b.run(); CHECK(r.disabledReasons.empty()); CHECK(!r.memberDisabledReasons.empty());
  CHECK(r.families[member].kept); CHECK(!r.families[dead].kept); CHECK(!r.assumptions.contains("A12"));
  b.m.libraryMemberNamesComputed = true;
  r = b.run(); CHECK(!r.families[member].kept); CHECK(r.assumptions.contains("A12"));
  auto report = renderPruningReport(b.m,r).value();
  CHECK(!embeddedLibraryData().memberInventoryProvenance.empty());
  CHECK(report.find("library member inventory: " + std::string(embeddedLibraryData().memberInventoryProvenance)) != report.npos);
  CHECK(report.find("pruning assumption: A12 library-spelled-members\n") != report.npos);
  CHECK(report.find("member units kept=0 removed=1\n") != report.npos);
  b.m.libraryMemberNames.insert("unused"); r = b.run(); CHECK(r.families[member].kept);
  CHECK(r.assumptions.contains("A12")); // Consistent with A11 even without a member deletion.
  b.m.libraryMemberNames.clear();
  const auto enabled = serializePruningModel(b.m).value();
  b.m.assumeLibrarySpelledMembers = false;
  const auto disabled = serializePruningModel(b.m).value(); CHECK(enabled != disabled);
  CHECK(!parsePruningModel(disabled).value().assumeLibrarySpelledMembers);
  CHECK(parsePruningModel(enabled).value().assumeLibrarySpelledMembers);
  r = b.run(); CHECK(r.families[member].kept); CHECK(!r.families[dead].kept);
  CHECK(!r.assumptions.contains("A12")); CHECK(r.assumptions.contains("A11"));
  report = renderPruningReport(b.m,r).value();
  CHECK(report.find("pruning assumption disabled: A12 library-spelled-members (--no-assume=library-spelled-members)") != report.npos);
  test::TempDirectory temp; PruneWorkerOptions o; o.temporaryRoot = temp.path; o.executable = test::helper;
  o.runner = [&](const ProcessSpec&) -> Result<ProcessResult> {
    ProcessResult response; response.exitCode = 0; response.stdoutText = enabled; return response;
  };
  CHECK(extractPruning(b.m,o).value().disabledReasons.back().starts_with("pruning worker changed immutable input"));
}

TEST(prune_never_removed_rules_and_nonroots) {
  for (int rule = 0; rule < 10; ++rule) {
    Model b; auto id = b.add("candidate","int candidate;",FamilyKind::variable); auto& f = b.m.families[id];
    if (rule == 0) f.pinned = "macro invocation";
    if (rule == 1) f.neverRemoved = "specified policy";
    if (rule == 2) f.nonMemberOperator = true;
    if (rule == 3) f.namespaceUsing = true;
    if (rule == 4) f.externCDefinition = true;
    if (rule == 5) f.systemSpecialization = true;
    if (rule == 6) f.root = "not inert (class type)";
    if (rule == 7) b.m.keepNames.insert("candidate");
    if (rule == 8) b.m.annotations.push_back({0,id});
    CHECK_EQ(b.run().families[id].kept,rule != 9);
  }
}
TEST(prune_adl_dialects_and_all_registry_names) {
  const auto& registry = embeddedAdlRegistry(); CHECK(registry.digest.starts_with("sha256:")); CHECK_EQ(registry.digest.size(),71u);
  for (const auto* dialect : {"c++11","gnu++11","c++14","gnu++14","c++17","gnu++17","c++20","gnu++20","c++23","gnu++23"}) {
    CHECK(registry.supports(dialect)); auto names = registry.names(dialect); const bool modern = std::string_view(dialect).ends_with("20") || std::string_view(dialect).ends_with("23");
    auto expected = std::set<std::string, std::less<>>{"swap", "begin", "end", "make_error_code", "make_error_condition",
      "abs", "acos", "asin", "atan", "atan2", "cos", "cosh", "exp", "log", "log10", "pow", "sin", "sinh", "sqrt", "tan", "tanh"};
    if (modern || std::string_view(dialect).ends_with("17")) expected.insert("get");
    if (modern) for (const auto* hook : {"iter_move","iter_swap","rbegin","rend","size","cbegin","cend","crbegin","crend",
        "ssize","empty","data","cdata","strong_order","weak_order","partial_order","from_stream"}) expected.insert(hook);
    CHECK_EQ(names,expected);
    for (const auto& name : names) {
      Model b; b.m.dialect = dialect; b.add(name,"void unrelated(){}");
      auto r = b.run(); CHECK(r.families[0].kept); CHECK_EQ(r.families[0].reason,"never removed: ADL name (A6)");
    }
  }
  for (const auto* dialect : {"c++98","gnu++03","c++26","gnu++26","c++2b",""}) { Model b; b.m.dialect = dialect; b.add("x","int x;"); CHECK(!b.run().disabledReasons.empty()); }
  CHECK(!parseAdlRegistry("c++20 x ADL [clause] x verified\n").ok());
  bool delegated = false;
  for (const auto& row : registry.entries) if (row.name == "cdata") delegated |= row.mechanism.starts_with("public-CPO-overretention");
  CHECK(delegated);
}
TEST(prune_dependent_name_all_namespaces_and_hidden_friends) {
  Model b; auto main = b.add("main","int main(){return 0;}"); auto a = b.add("customize","void first(){}");
  auto hidden = b.add("Box","struct Box{};",FamilyKind::record); auto dead = b.add("dead","void dead(){}");
  b.m.families[main].dependentNames.insert("customize"); b.m.families[hidden].callableNames.insert("customize");
  auto r = b.run(); CHECK(r.families[a].kept); CHECK(r.families[hidden].kept); CHECK(!r.families[dead].kept);
  CHECK_EQ(r.families[a].reason,"dependent name: customize");
}
TEST(prune_dependent_members_and_qualifiers_use_whole_carrier_closure) {
  Model b;
  auto main = b.add("main","int main(){run<First,Second>();}");
  auto run = b.add("run","template<class A,class B> void run(){ using T=std::conditional_t<sizeof(long)==4,A,B>; T x; x.template f<int>(); T::e(); }");
  auto first = b.add("First","struct First : Base { using nested = Nested; };",FamilyKind::record);
  auto second = b.add("Second","struct Second { Target operator->(); operator Target(); };",FamilyKind::record);
  auto base = b.add("Base","struct Base { template<class T> void f(); static void e(); };",FamilyKind::record);
  auto nested = b.add("Nested","struct Nested { using type = Target; };",FamilyKind::record);
  auto target = b.add("Target","struct Target { void pasted_member(); };",FamilyKind::record);
  auto unrelated = b.add("Unrelated","struct Unrelated { void f(); static void e(); };",FamilyKind::record);
  // Even without an instantiation edge to the judge's selected alternative,
  // both inputs and every carrier's bases/aliases/returns survive by rule 2.
  // Member names are not indexed, so a same-name record is still removable.
  auto r = b.run(); CHECK(r.disabledReasons.empty());
  for (auto id : {main,run,first,second,base,nested,target}) CHECK(r.families[id].kept);
  CHECK(!r.families[unrelated].kept);
  // A type synthesized in a seen instantiation instead follows rule 4.
  b.m.families[second].edges = {unrelated}; CHECK(b.run().families[unrelated].kept);
}
TEST(prune_static_tokens_literals_macros_and_self_exclusion) {
  Model b; auto self = b.add("self","int self(){return self();}");
  auto ignored = b.add("ignored","void ignored(){}"); auto a = b.add("suffix","void other(){}");
  auto numeric = b.add("_unit","void units(){}");
  b.m.text += "#define KEEP suffix\n// ignored\nconst char* s = \"ignored\";\nauto v = 17_unit;\n";
  auto r = b.run(); CHECK(!r.families[self].kept); CHECK(!r.families[ignored].kept); CHECK(r.families[a].kept); CHECK(r.families[numeric].kept);
  CHECK(staticNameInvariant(b.m,r,"// self ignored\n\"ignored\"\n").ok());
  CHECK(!staticNameInvariant(b.m,r,"int ignored;").ok());
  CHECK(!staticNameInvariant(b.m,r,"int se\\\nlf;").ok());
  CHECK(staticNameInvariant(b.m,r,"SELF").ok());
}
TEST(prune_system_edges_transitive_cycles_positive_only) {
  Model b; auto main = b.add("main","int main(){}"); auto hook = b.add("hook","void hook(){}"); b.add("unused","void unused(){}");
  b.m.systemEntities = {{"instantiate",{}, {1}}, {"body",{hook}, {0}}}; b.m.systemUses = {{main,0}};
  auto r = b.run(); CHECK(r.families[hook].kept); CHECK(!r.families.back().kept);
  b.m.systemUses.clear(); CHECK(!b.run().families[hook].kept);
  b.m.systemUses = {{noFamily,0}}; CHECK(b.run().families[hook].kept);
}
TEST(prune_instantiation_effects_preserve_reverse_transitive_triggers) {
  Model b;
  auto effect = b.add("Inject","template<class T> struct Inject {};",FamilyKind::record);
  b.m.families[effect].instantiationEffect = "template static or thread storage not inert";
  auto alias = b.add("Alias","using Alias = Inject<int>;",FamilyKind::alias);
  auto trigger = b.add("Unused","struct Unused { decltype(sizeof(Alias)) dormant(); };",FamilyKind::record);
  auto pasted = b.add("Pasted","struct Pasted {};",FamilyKind::record);
  b.m.families[pasted].edges = {effect}; // Expanded name, absent from raw text.
  auto system = b.add("Default","struct Default {};",FamilyKind::record);
  b.m.systemEntities = {{"library default",{}, {1}}, {"library alias",{effect}, {0}}};
  b.m.systemUses = {{system,0}};
  auto paired = b.add("Paired","struct Paired {};",FamilyKind::record);
  b.m.unions = {{paired,trigger}};
  auto dead = b.add("Dead","struct Dead {};",FamilyKind::record);
  b.add("main","int main(){}");
  auto r = b.run(); CHECK(r.disabledReasons.empty());
  for (auto id : {effect,alias,trigger,pasted,system,paired}) CHECK(r.families[id].kept);
  CHECK(!r.families[dead].kept);
  CHECK(r.families[trigger].reason.starts_with("instantiation effect:"));
  CHECK_EQ(r.families[trigger].cause,effect);
  CHECK(renderPruningReport(b.m,r).value().find("instantiation effect:") != std::string::npos);
  auto roundtrip = parsePruningModel(serializePruningModel(b.m).value()); CHECK(roundtrip.ok());
  CHECK_EQ(roundtrip.value().families[effect].instantiationEffect,"template static or thread storage not inert");
  CHECK_EQ(computeRetention(roundtrip.value()).value().families[trigger].reason,r.families[trigger].reason);
}
TEST(prune_instantiation_effects_reverse_dependent_lookup_and_associations) {
  Model b;
  auto effect = b.add("Inject","struct Inject {};",FamilyKind::record);
  b.m.families[effect].instantiationEffect = "template static or thread storage not inert";
  auto callback = b.add("customize","void customize() { Inject x; }");
  auto key = b.add("Key","struct Key {};",FamilyKind::record);
  b.m.families[callback].associatedTypes = {key};
  auto adl = b.add("probe","template<class T> void probe() {} ");
  b.m.families[adl].dependentNames.insert("customize");
  auto trigger = b.add("Unused","struct Unused { Key x; };",FamilyKind::record);
  const auto r = b.run(); CHECK(r.disabledReasons.empty());
  for (auto id : {effect,callback,key,adl,trigger}) CHECK(r.families[id].kept);
}
TEST(prune_ordinary_member_declarations_reach_owner_effect_triggers) {
  for (bool assume : {true,false}) {
    Model b; b.m.libraryMemberNamesComputed = true; b.m.assumeInstantiationEffects = assume;
    const auto source = b.add("Source","struct Source {};",FamilyKind::record);
    b.m.families[source].instantiationEffect = "template static storage";
    const auto owner = b.add("Carrier","struct Carrier {};",FamilyKind::record);
    const auto member = b.add("dormant","void dormant();");
    b.m.families[member].memberOf = owner;
    b.m.families[member].memberName = "dormant";
    b.m.families[member].edges = {source}; // Signature/default/constraint dependency.
    const auto unrelated = b.add("unrelated","void unrelated();");
    b.m.families[unrelated].memberOf = owner;
    b.m.families[unrelated].memberName = "unrelated";
    const auto trigger = b.add("trigger","void trigger(){}");
    b.m.families[trigger].edges = {owner};
    b.add("main","int main(){}");
    const auto r = b.run(); CHECK(r.disabledReasons.empty());
    for (auto id : {source,owner,member,trigger}) CHECK(r.families[id].kept);
    CHECK(r.families[trigger].reason.starts_with("instantiation effect:"));
    CHECK_EQ(r.families[trigger].cause,source);
    CHECK(!r.families[unrelated].kept);
  }
}
TEST(prune_instantiation_effects_refuse_unbounded_system_dispatch) {
  for (int form = 0; form < 3; ++form) {
    Model b;
    auto effect = b.add("Reg","struct Reg {};",FamilyKind::record);
    b.m.families[effect].instantiationEffect = "template static or thread storage not inert";
    auto hook = b.add("hook","void hook() { Reg x; }");
    if (form == 0) b.m.families[hook].callableNames.insert("swap");
    if (form == 1) b.m.families[hook].nonMemberOperator = true;
    if (form == 2) b.m.families[hook].systemSpecialization = true;
    b.add("dead","void dead(){}");
    const auto r = b.run(); CHECK(!r.disabledReasons.empty());
    CHECK(r.disabledReasons.back().starts_with("unbounded system instantiation effects through hook"));
    for (const auto& f : r.families) CHECK(f.kept);
  }
}
TEST(prune_owner_dispatch_preserves_nonmember_name_effect_paths) {
  Model b;
  b.m.libraryMemberNamesComputed = true;
  const auto owner = b.add("Owner","struct Owner {};",FamilyKind::record);
  const auto effect = b.add("Effect","struct Effect {};",FamilyKind::record);
  b.m.families[effect].instantiationEffect = "template static storage";
  const auto unit = b.add("unit","void free_hook() {}");
  auto& f = b.m.families[unit];
  f.memberOf = owner; f.memberName = "mandatory";
  f.memberBlocker = "D3e rule 1: excluded kind or virtual";
  f.names = {"free_hook"}; f.callableNames = {"free_hook"}; f.edges = {effect};
  const auto caller = b.add("dormant","void dormant() { free_hook(); }");
  const auto dead = b.add("dead","void dead() {}");
  b.add("main","int main() {}");
  auto r = b.run(); CHECK(r.disabledReasons.empty());
  for (auto id : {owner,effect,unit,caller}) CHECK(r.families[id].kept);
  CHECK(r.families[caller].reason.starts_with("instantiation effect:"));
  CHECK(!r.families[dead].kept);
  // Owner dispatch does not exempt a real free/system hook in the same unit.
  b.m.families[unit].callableNames.insert("swap");
  CHECK(!b.run().disabledReasons.empty());
  b.m.families[unit].callableNames.erase("swap");
  b.m.families[unit].systemSpecialization = true;
  CHECK(!b.run().disabledReasons.empty());
}
TEST(prune_instantiation_effects_hidden_hooks_need_verified_own_record_carriers) {
  for (bool verified : {false,true}) {
    Model b;
    auto record = b.add("S","template<class T> struct S {};",FamilyKind::record);
    auto unrelated = b.add("Other","struct Other {};",FamilyKind::record);
    auto trigger = b.add("Unused","struct Unused { S<int> x; };",FamilyKind::record);
    auto& f = b.m.families[record];
    f.instantiationEffect = "template static or thread storage not inert"; f.nonMemberOperator = true;
    f.associatedTypes = {record,unrelated}; // A nonempty list alone proves nothing.
    f.boundedSystemHooks = verified;
    const auto r = b.run(); CHECK_EQ(r.disabledReasons.empty(),verified);
    CHECK(r.families[trigger].kept);
    const auto decoded = parsePruningModel(serializePruningModel(b.m).value()).value();
    CHECK_EQ(decoded.families[record].boundedSystemHooks,verified);
  }
}
TEST(prune_all_disable_reasons_accumulate) {
  Model b; b.add("x","int x;"); b.m.unknownInUserCode = true; b.m.dialect = "c++98";
  b.m.includes = {"ext/rope","ext/pb_ds/assoc_container.hpp","bits/extc++.h","vendor.h"};
  b.m.disabledReasons = {"parse error","limit","parse error"};
  auto r = b.run(); CHECK_EQ(r.disabledReasons.size(),8u); CHECK(r.families[0].kept); CHECK(!r.assumptions.contains("A6")); CHECK(!r.assumptions.contains("A7")); CHECK(!r.assumptions.contains("A11"));
  CHECK(!staticNameInvariant(b.m,r,"").ok());
}
TEST(prune_predefined_macros_source_overrides_and_pasting) {
  for (int scenario = 0; scenario < 13; ++scenario) {
    Model b; b.add("x","int x;"); PruneMacroExpansion e; e.name = "__GNUC__"; e.predefined = true;
    e.parse = known("4"); e.judge = known("4");
    if (scenario == 1) e.judge = known("15");
    if (scenario == 2) e.judge = {};
    if (scenario == 3) { e.paste = true; e.externalPasteOperand = true; }
    if (scenario == 4) { e.paste = true; e.pasteOperandsKnown = false; }
    if (scenario == 5) { e.judge = known("15"); e.condition = true; }
    if (scenario == 6) { e.judge = known("15"); e.user = false; }
    if (scenario == 7) { e.predefined = false; e.paste = true; }
    if (scenario == 8) e.judge = {MacroState::undefined,{}, {FactOrigin::a4}};
    if (scenario == 9) e.parse = {};
    if (scenario == 10) e.judge = {MacroState::definedUnknown,{}, {FactOrigin::specialBuiltin}};
    if (scenario == 11) e.parse = {MacroState::definedUnknown,{}, {FactOrigin::compilerBuiltin}};
    if (scenario == 12) e.judge = known("");
    b.m.expansions.push_back(e);
    CHECK_EQ(b.run().disabledReasons.empty(),scenario == 0 || (scenario >= 5 && scenario <= 7));
    if (scenario == 1) CHECK_EQ(b.run().disabledReasons.front(),"predefined macro __GNUC__ differs: judge 15, parse 4");
    if (scenario == 2) CHECK_EQ(b.run().disabledReasons.front(),"predefined macro __GNUC__: unknown judge fact");
    if (scenario == 8) CHECK_EQ(b.run().disabledReasons.front(),"predefined macro __GNUC__ differs: judge undefined, parse 4");
    if (scenario == 9 || scenario == 11) CHECK_EQ(b.run().disabledReasons.front(),"predefined macro __GNUC__: unknown parse fact");
    if (scenario == 10) CHECK_EQ(b.run().disabledReasons.front(),"predefined macro __GNUC__: unknown judge fact");
    if (scenario == 12) CHECK_EQ(b.run().disabledReasons.front(),"predefined macro __GNUC__ differs: judge <empty>, parse 4");
  }
  Model b; PruneMacroExpansion e; e.name = "X"; e.predefined = true; e.parse = known("a + b"); e.judge = known("a+b"); b.m.expansions.push_back(e); CHECK(b.run().disabledReasons.empty());
}
TEST(prune_equal_integer_literal_predefined_macros) {
  // D3d-4: GCC and Clang spell limit macros differently (0x7f vs 127).
  auto run = [](std::string parse, std::string judge, auto&& tweak) {
    Model b; b.add("x","int x;"); PruneMacroExpansion e; e.name = "__SCHAR_MAX__"; e.predefined = true; e.offset = 0;
    e.parse = known(parse); e.judge = known(judge);
    b.m.parseWidths = LiteralWidths{32,64,64};
    for (auto [name,value] : {std::pair{"__CHAR_BIT__","8"},{"__SIZEOF_INT__","4"},{"__SIZEOF_LONG__","8"},{"__SIZEOF_LONG_LONG__","8"}})
      b.m.judgeInitial[name] = known(value);
    tweak(b.m,e); b.m.expansions.push_back(e);
    return b.run().disabledReasons;
  };
  auto none = [](PruningModel&, PruneMacroExpansion&) {};
  for (auto [parse,judge] : {std::pair{"127","0x7f"},{"2147483647","0x7fffffff"},{"9223372036854775807L","0x7fffffffffffffffL"},
                             {"9223372036854775807LL","0x7fffffffffffffffLL"},{"4294967295U","0xffffffffU"},{"18446744073709551615UL","0xffffffffffffffffUL"},
                             {"18446744073709551615ul","0xFFFFFFFFFFFFFFFFLU"},{"8","010"},{"0","0x0"}})
    CHECK(run(parse,judge,none).empty());
  // Different type: hex picks unsigned int, decimal picks long (32-bit int).
  for (auto [parse,judge] : {std::pair{"4294967295","0xffffffff"},{"202002","202002L"},{"16","16UL"},{"1","1u"},{"1L","1LL"}})
    CHECK_EQ(run(parse,judge,none).size(),1u);
  // Different value, unclassifiable literal, or no standard type.
  for (auto [parse,judge] : {std::pair{"127","0x7e"},{"127","(127)"},{"127","+127"},{"127","0b1111111"},{"127","127lL"},
                             {"127","1'27"},{"127","127z"},{"18446744073709551615","0xffffffffffffffff"},{"0x7f","0x"}})
    CHECK_EQ(run(parse,judge,none).size(),1u);
  // Spelling-sensitive uses and unknown integer models.
  CHECK_EQ(run("127","0x7f",[](auto&, auto& e) { e.hashOperator = true; }).front(),"predefined macro __SCHAR_MAX__ differs: judge 0x7f, parse 127");
  CHECK_EQ(run("127","0x7f",[](auto&, auto& e) { e.paste = true; }).front(),"predefined macro __SCHAR_MAX__ differs: judge 0x7f, parse 127");
  CHECK_EQ(run("127","0x7f",[](auto& m, auto&) { m.sourceDirectives = {{0,3}}; }).size(),1u);
  CHECK_EQ(run("127","0x7f",[](auto& m, auto&) { m.parseWidths.reset(); }).size(),1u);
  CHECK_EQ(run("127","0x7f",[](auto& m, auto&) { m.judgeInitial.erase("__SIZEOF_INT__"); }).size(),1u);
  CHECK_EQ(run("127","0x7f",[](auto& m, auto&) { m.judgeInitial["__SIZEOF_INT__"].provenance = {FactOrigin::commandOperation}; }).size(),1u);
  CHECK_EQ(run("127","0x7f",[](auto&, auto& e) { e.judge.definition->functionLike = true; }).size(),1u);
  // Type agreement is required under the judge widths too: 16-bit int there.
  CHECK_EQ(run("40000","0x9c40",[](auto& m, auto&) { m.judgeInitial["__SIZEOF_INT__"] = known("2"); }).size(),1u);
  CHECK(run("40000","0x9c40",none).empty());
  // The protocol round-trips the new fields.
  Model b; b.add("x","int x;"); b.m.parseWidths = LiteralWidths{32,32,64};
  PruneMacroExpansion e; e.hashOperator = true; b.m.expansions.push_back(e);
  auto round = parsePruningModel(serializePruningModel(b.m).value());
  CHECK(round.ok()); CHECK(round.value().parseWidths == b.m.parseWidths); CHECK(round.value().expansions[0].hashOperator);
  b.m.parseWidths = LiteralWidths{0,32,64}; CHECK(!serializePruningModel(b.m).ok());
}
TEST(prune_profile_inert_exempts_only_exact_profile_occurrences) {
  const auto p = lookupProfile("atcoder-gcc15-gnupp23").value();
  for (const auto& inert : p.inert) for (const auto& option : {inert, inert + "0"}) {
    const auto local = parseCompileCommand({"g++", "-isystem", "sys", option, "main.cpp"}, std::filesystem::current_path()).value();
    Configuration c; c.command = flatCommand(local, &p, "g++", false).value();
    c.probe.triple = p.triple; c.probe.dialect = p.dialect;
    CHECK_EQ(c.command.pruningAllowed().reasons, (std::vector<std::string>{"unmodelled option: " + option}));
    CHECK_EQ(c.command.args[1].classification, Class::unmodelled);
    CHECK(!c.command.args[1].profilePreprocessingInert);
    ResolveResult flat; flat.text = "int main(){}\n";
    const auto m = preparePruning(c, {}, flat, &p).value();
    CHECK_EQ(m.disabledReasons.front(), "unmodelled option: " + option);
    CHECK_EQ(std::count(m.disabledReasons.begin(), m.disabledReasons.end(), "untranslated semantic option: " + option), option == inert ? 2 : 1);
    CHECK(std::find(m.parseArgv.begin(), m.parseArgv.end(), option) == m.parseArgv.end());
    const auto build = c.command.buildArgv("flat.cpp", "program", true, {});
    CHECK_EQ(std::count(build.begin(), build.end(), option), option == inert ? 2 : 1);
  }
}
TEST(prune_unknown_location_uses_resolver_decision_including_elif) {
  Configuration c; c.probe.triple = "x86_64-unknown-linux-gnu"; c.probe.dialect = {"gnu++20",false,202002};
  ResolveResult flat;
  flat.text = "#if 0\n#elif __UNKNOWN__\nint x;\n#endif\n";
  flat.lineMap = {SourceLocation{"misleading.hpp",1}};
  flat.unknownInUserCode = true;
  flat.decisions = {{30,SourceLocation{"actual.hpp",90},"if",Decision::falseValue,"",{},{}},
                    {31,SourceLocation{"actual.hpp",91},"elif",Decision::unknown,"",{},{}}};
  const auto m = preparePruning(c, {}, flat).value();
  const std::vector<std::string> expected{"unknown conditional in user code at actual.hpp:91 (#elif)"};
  CHECK_EQ(m.disabledReasons, expected);
  const auto roundtrip = parsePruningModel(serializePruningModel(m).value()).value();
  CHECK_EQ(computeRetention(roundtrip).value().disabledReasons, expected);
  flat.decisions[1].original.reset();
  CHECK_EQ(preparePruning(c, {}, flat).value().disabledReasons.front(), "unknown conditional in user code at raw.cpp:31 (#elif)");
}
TEST(prune_directives_pin_generated_markers_do_not) {
  Model b; b.add("x","void x(){\n#line 20 \"file\"\n}\n");
  b.m.generatedMarkers = {{10,26}}; CHECK(!b.run().families[0].kept);
  b.m.sourceDirectives = {{10,26}}; CHECK(b.run().families[0].kept);
}
TEST(prune_model_protocol_roundtrip_and_rejections) {
  Model b; b.add("main","int main(){}"); b.add("f","int f();");
  b.m.families[0].edges = {1}; b.m.families[0].original = SourceLocation{"a.hpp",42}; b.m.lineMap = {std::nullopt,SourceLocation{"a.hpp",42}};
  b.m.judgeInitial["__GNUC__"] = known("15"); b.m.expansions.push_back({0,"X",true,false,true,known("1"),known("2"),false,false,true});
  auto encoded = serializePruningModel(b.m); CHECK(encoded.ok()); auto decoded = parsePruningModel(encoded.value()); CHECK(decoded.ok());
  CHECK_EQ(serializePruningModel(decoded.value()).value(),encoded.value());
  auto previous = encoded.value(); previous.replace(0,15,"DISTILL-PRUNE-6");
  auto rejected = parsePruningModel(previous); CHECK(!rejected.ok());
  CHECK(rejected.error().message.find("invalid pruning protocol version") != std::string::npos);
  CHECK(!parsePruningModel(encoded.value()+" garbage").ok());
  CHECK(!parsePruningModel(encoded.value().substr(0,encoded.value().size()/2)).ok());
  CHECK(!parsePruningModel("DISTILL-PRUNE-9").ok());
  b.m.families[0].edges = {100}; CHECK(!computeRetention(b.m).ok()); b.m.families[0].edges.clear();
  b.m.families[1].ranges = b.m.families[0].ranges; CHECK(!computeRetention(b.m).ok());
}
TEST(prune_protocol_errors_locate_field_and_line) {
  for (const auto* value : {"oops", "11", "65"}) {
    auto r = parsePruningModel(std::string("DISTILL-PRUNE-8 \"first\nsecond\" \"c++20\" 1 ") + value);
    CHECK(!r.ok()); CHECK(r.error().message.find("field model.families[0].kind, line 2") != std::string::npos);
  }
  auto string = parsePruningModel("DISTILL-PRUNE-8 unquoted");
  CHECK(!string.ok()); CHECK(string.error().message.find("field model.text, line 1") != std::string::npos);
  auto truncated = parsePruningModel("DISTILL-PRUNE-8 \"first\nsecond");
  CHECK(!truncated.ok()); CHECK(truncated.error().message.find("field model.text, line 2") != std::string::npos);
  PruningModel m; m.text = "first\n" + std::string(1000,' '); m.keepOffsets = {987};
  auto encoded = serializePruningModel(m); CHECK(encoded.ok());
  auto bad = encoded.value(); const auto offset = bad.find("987 "); CHECK(offset != bad.npos);
  bad.replace(offset,3,"9999");
  auto invalid = parsePruningModel(bad); CHECK(!invalid.ok());
  CHECK(invalid.error().message.find("field model.keepOffsets[0], line 2, byte " + std::to_string(offset)) != std::string::npos);
}
TEST(prune_prepare_selected_configuration_and_provenance) {
  Configuration c; c.command = parseCompileCommand({"g++","-std=c++23","-DU=1","-UU","-DU=2","-funsigned-char","-march=native","-O2","main.cpp"},std::filesystem::current_path()).value();
  c.probe.triple = "x86_64-w64-mingw32"; c.probe.dialect = {"c++23",false,202302};
  c.probe.searchLists.directories = {{"/cxx",IncludeKind::system,false,false},{"/gcc/internal",IncludeKind::system,false,true},{"/crt",IncludeKind::system,false,false}};
  ResolveResult flat; flat.text = "#line 1 \"a\"\nvoid unused(){}\n"; flat.lineMap = {std::nullopt,SourceLocation{"a",1}};
  auto m = preparePruning(c,{},flat); CHECK(m.ok()); CHECK_EQ(m.value().generatedMarkers.size(),1u);
  const auto& args = m.value().parseArgv;
  CHECK(std::find(args.begin(),args.end(),"-fgnuc-version=4.2.1") != args.end());
  CHECK(std::find(args.begin(),args.end(),"-DU=1") < std::find(args.begin(),args.end(),"-UU"));
  CHECK(std::find(args.begin(),args.end(),"-UU") < std::find(args.begin(),args.end(),"-DU=2"));
  CHECK(std::find(args.begin(),args.end(),"/gcc/internal") == args.end());
  CHECK(std::find(args.begin(),args.end(),"-undef") == args.end());
  CHECK(!m.value().disabledReasons.empty()); CHECK_EQ(m.value().untranslatedOptions,(std::vector<std::string>{"-march=native"}));
}
TEST(prune_prepare_cc1_translation_table) {
  struct Row {
    std::vector<std::string> driver, cc1;
    bool exceptions = true;
    std::string triple = "x86_64-unknown-linux-gnu", disabled = {};
  };
  // These are complete argv deltas, not just presence checks: driver-only
  // options must never sneak into CreateFromArgs beside the translated ones.
  const std::vector<Row> rows = {
    {{"-DX=1","-UX","-DX=2"},{"-DX=1","-UX","-DX=2"}},
    {{"-Iinc","-iquote","quote","-isystem","sys"},{}}, // Probed search list is authoritative.
    {{"-oout","-MD","-MFdep.d","-MTtarget"},{}},
    {{"-fsigned-char"},{}},
    {{"-funsigned-char"},{"-fno-signed-char"}},
    {{"-funsigned-char","-fsigned-char"},{}},
    {{"-fsigned-char","-funsigned-char"},{"-fno-signed-char"}},
    {{"-fshort-wchar"},{"-fwchar-type=short","-fno-signed-wchar"}},
    {{"-fwrapv"},{},true,"x86_64-unknown-linux-gnu","-fwrapv"},
    {{"-fno-wrapv"},{},true,"x86_64-unknown-linux-gnu","-fno-wrapv"},
    {{"-fno-exceptions"},{},false},
    {{"-fno-rtti"},{"-fno-rtti"}},
    {{"-pthread","-U_REENTRANT"},{"-pthread","-U_REENTRANT"}},
    {{"-O0"},{"-O0"}}, {{"-O1"},{"-O1"}}, {{"-O2"},{"-O2"}},
    {{"-O3"},{"-O3"}}, {{"-Os"},{"-Os"}}, {{"-Og"},{"-Og"}},
    {{"-O2","-O0"},{"-O2","-O0"}},
    {{"-m32"},{},true,"i386-unknown-linux-gnu"},
    {{"-m32","-m64"},{},true},
    {{"-g","-Wall","-pipe","-static","-lm"},{}},
    {{"-march=haswell"},{},true,"x86_64-unknown-linux-gnu","-march=haswell"},
    {{"-march=native"},{},true,"x86_64-unknown-linux-gnu","-march=native"},
    {{"-mtune=generic"},{},true,"x86_64-unknown-linux-gnu","-mtune=generic"},
    {{"-mcpu=native"},{},true,"x86_64-unknown-linux-gnu","-mcpu=native"},
    {{"-fmodules"},{},true,"x86_64-unknown-linux-gnu","-fmodules"},
    {{"-fopenmp"},{},true,"x86_64-unknown-linux-gnu","-fopenmp"},
    {{"-Ofast"},{},true,"x86_64-unknown-linux-gnu","-Ofast"},
  };
  Configuration c; c.probe.triple = "x86_64-unknown-linux-gnu"; c.probe.dialect = {"gnu++20",true,202002};
  auto defaults = preparePruning(c,{},{}).value().parseArgv;
  defaults.resize(defaults.size()-3); // Default exception pair and flat.cpp.
  for (const auto& row : rows) {
    std::vector<std::string> argv{"g++"}; argv.insert(argv.end(),row.driver.begin(),row.driver.end()); argv.push_back("main.cpp");
    c.command = parseCompileCommand(argv,std::filesystem::current_path()).value();
    auto m = preparePruning(c,{},{}); CHECK(m.ok());
    auto expected = defaults;
    *std::next(std::find(expected.begin(),expected.end(),"-triple")) = row.triple;
    expected.insert(expected.end(),row.cc1.begin(),row.cc1.end());
    if (row.exceptions) expected.insert(expected.end(),{"-fcxx-exceptions","-fexceptions"});
    expected.push_back("flat.cpp");
    CHECK_EQ(m.value().parseArgv,expected);
    auto reasons = c.command.pruningAllowed().reasons;
    if (!row.disabled.empty()) reasons.push_back("untranslated semantic option: " + row.disabled);
    CHECK_EQ(m.value().disabledReasons,reasons);
  }
}
TEST(prune_prepare_unsigned_target_default_and_signed_override) {
  // LLVM 23 cc1 is signed by default on ARM too. A signed selection is
  // expressible by omitting the negative flag, independently of target default.
  for (const auto* triple : {"aarch64-unknown-linux-gnu","x86_64-unknown-linux-gnu"}) {
    for (const auto& flags : std::vector<std::vector<std::string>>{{},{"-fsigned-char"},{"-funsigned-char","-fsigned-char"},{"-fsigned-char","-funsigned-char"}}) {
      Configuration c; c.probe.triple = triple; c.probe.dialect = {"gnu++20",true,202002};
      c.probe.macros["__CHAR_UNSIGNED__"] = Macro{false,{},false,"1"};
      std::vector<std::string> argv{"g++"}; argv.insert(argv.end(),flags.begin(),flags.end()); argv.push_back("main.cpp");
      c.command = parseCompileCommand(argv,std::filesystem::current_path()).value();
      auto m = preparePruning(c,{},{}).value(); CHECK(m.disabledReasons.empty());
      CHECK_EQ(std::count(m.parseArgv.begin(),m.parseArgv.end(),"-fno-signed-char"),
               flags.empty() || flags.back() == "-funsigned-char" ? 1 : 0);
      CHECK(std::find(m.parseArgv.begin(),m.parseArgv.end(),"-fsigned-char") == m.parseArgv.end());
    }
  }
}
TEST(prune_prepare_cpp_defaults_and_explicit_disables) {
  for (int disables = 0; disables < 4; ++disables) {
    std::vector<std::string> argv{"g++","-std=gnu++20"};
    if (disables & 1) argv.push_back("-fno-exceptions");
    if (disables & 2) argv.push_back("-fno-rtti");
    argv.push_back("main.cpp");
    Configuration c; c.command = parseCompileCommand(argv,std::filesystem::current_path()).value();
    c.probe.triple = "x86_64-unknown-linux-gnu"; c.probe.dialect = {"gnu++20",true,202002};
    auto m = preparePruning(c,{},{}); CHECK(m.ok()); CHECK(m.value().disabledReasons.empty());
    const auto& args = m.value().parseArgv;
    auto has = [&](const char* flag) { return std::find(args.begin(),args.end(),flag) != args.end(); };
    CHECK(has("-fsyntax-only")); CHECK(has("-fmath-errno")); CHECK(has("-fdeprecated-macro"));
    CHECK_EQ(has("-fcxx-exceptions"),!(disables & 1));
    CHECK_EQ(has("-fexceptions"),!(disables & 1));
    CHECK_EQ(has("-fno-rtti"),bool(disables & 2));
    CHECK(!has("-fno-builtin")); CHECK(!has("-ffreestanding"));
  }
}
TEST(prune_prepare_pragmas_allow_only_audited_effects_and_preserve_directives) {
  Configuration c; c.probe.triple = "x86_64-unknown-linux-gnu"; c.probe.dialect = {"gnu++20",false,202002};
  for (const auto* pragma : {"GCC optimize(\"O3\")", "GCC target(\"avx2\")", "GCC push_options", "GCC pop_options", "GCC reset_options",
       "GCC diagnostic push", "GCC diagnostic ignored \"-Wunused-variable\"", "GCC diagnostic pop", "clang diagnostic pop",
       "pack(push,1)", "pack(pop)", "message(\"fixture\")", "warning(disable:4996)", "once"}) {
    ResolveResult flat; flat.text = "#pragma " + std::string(pragma) + "\nint unused;\n";
    auto m = preparePruning(c,{},flat); CHECK(m.ok()); CHECK(m.value().disabledReasons.empty());
    CHECK_EQ(m.value().sourceDirectives.size(),1u);
    CHECK_EQ(m.value().text.substr(m.value().sourceDirectives[0].begin,m.value().sourceDirectives[0].end),"#pragma " + std::string(pragma) + "\n");
    CHECK(computeRetention(m.value()).value().disabledReasons.empty());
  }
  for (const auto* pragma : {"", "unknown", "GCC visibility push(hidden)", "GCC optimize_extra(\"O3\")", "GCC push_options unexpected",
       "clang attribute push", "omp parallel", "comment(linker,\"symbol\")", "once extra"}) {
    ResolveResult flat; flat.text = "#pragma " + std::string(pragma) + "\n";
    auto m = preparePruning(c,{},flat); CHECK(m.ok());
    CHECK_EQ(m.value().disabledReasons.size(),1u);
    CHECK(m.value().disabledReasons.front().starts_with("unsupported user pragma at line "));
  }
  ResolveResult flat; flat.text = "void crossed(){\n#pragma GCC diagnostic push\n}\n";
  auto m = preparePruning(c,{},flat).value();
  PruneFamily f; f.label = "crossed"; f.ranges = {{0,m.text.size()}}; m.families.push_back(f);
  auto r = computeRetention(m).value(); CHECK(r.disabledReasons.empty()); CHECK(r.families[0].kept);
  CHECK_EQ(r.families[0].reason,"pinned: source directive");
}
TEST(prune_worker_failures_use_bounded_core_runner) {
  Model b; b.add("x","int x;"); test::TempDirectory temp; PruneWorkerOptions o; o.temporaryRoot = temp.path; o.executable = test::helper;
  for (int scenario = 0; scenario < 8; ++scenario) {
    o.runner = [&](const ProcessSpec& spec) -> Result<ProcessResult> {
      CHECK_EQ(spec.argv[1],"--internal-prune"); CHECK(spec.timeout <= o.timeout); CHECK(std::filesystem::exists(spec.argv[2]));
      CHECK(spec.cwd.is_absolute()); CHECK(pathFromUtf8(spec.argv[2]).is_absolute());
      CHECK(!spec.memoryLimitBytes); // A condition-sized AS cap cannot host a Clang AST.
      if (scenario == 7) return Error{"spawn fixture failure"};
      ProcessResult r; r.exitCode = 0; r.stderrText = "diagnostic fixture\n" + std::string(3000,'x') + "OMITTED TAIL";
      if (scenario == 0) r.timedOut = true;
      if (scenario == 1) r.stdoutTruncated = true;
      if (scenario == 2) r.exitCode = 1;
      if (scenario == 3) r.stdoutText = "broken";
      if (scenario == 4) r.stdoutText = serializePruningModel(b.m).value();
      if (scenario == 5) { r.signal = 11; r.exitCode = 139; }
      if (scenario == 6) r.stderrTruncated = true;
      return r;
    };
    auto r = extractPruning(b.m,o); CHECK(r.ok()); CHECK_EQ(r.value().disabledReasons.empty(),scenario == 4);
    if (scenario != 4) {
      const auto& why = r.value().disabledReasons.back();
      for (const auto* key : {"exit_code=", "signal=", "timed_out=", "stdout_truncated=", "stderr_truncated=", "cwd=", "argv=[", "--internal-prune", "stderr (first 2048 bytes):"}) CHECK(why.find(key) != why.npos);
      std::istringstream argv(why.substr(why.find("argv=[")+6)); std::string executable;
      argv >> std::quoted(executable); CHECK_EQ(executable,pathToUtf8(o.executable));
      if (scenario != 7) { CHECK(why.find("diagnostic fixture\n") != why.npos); CHECK(why.find("OMITTED TAIL") == why.npos); }
      if (scenario == 0) CHECK(why.find("timed_out=true") != why.npos);
      if (scenario == 1) CHECK(why.find("stdout_truncated=true") != why.npos);
      if (scenario == 2) CHECK(why.find("exit_code=1;") != why.npos);
      if (scenario == 3) { CHECK(why.find("field version") != why.npos); CHECK(why.find("line 1") != why.npos); }
      if (scenario == 5) { CHECK(why.find("exit_code=139") != why.npos); CHECK(why.find("signal=11") != why.npos); }
      if (scenario == 6) CHECK(why.find("stderr_truncated=true") != why.npos);
      if (scenario == 7) CHECK(why.find("spawn fixture failure") != why.npos);
    }
    CHECK(std::filesystem::is_empty(temp.path));
  }
  o.timeout = std::chrono::milliseconds(0); CHECK(!extractPruning(b.m,o).value().disabledReasons.empty());
}

TEST(prune_exhaustive_three_family_graph_closure) {
  // Independent graph oracle over every directed graph and root subset. This
  // exercises cycles and processing order without reproducing the queue logic.
  for (unsigned graph = 0; graph < 512; ++graph) for (unsigned roots = 0; roots < 8; ++roots) {
    Model b;
    for (unsigned i = 0; i < 3; ++i) b.add("f"+std::to_string(i),"void body(){}");
    for (unsigned i = 0; i < 3; ++i) {
      if (roots & (1u<<i)) b.m.families[i].root = "explicit";
      for (unsigned j = 0; j < 3; ++j) if (graph & (1u<<(3*i+j))) b.m.families[i].edges.push_back(j);
    }
    unsigned expected = roots;
    for (unsigned path = 0; path < 3; ++path) for (unsigned i = 0; i < 3; ++i) if (expected & (1u<<i))
      for (unsigned j = 0; j < 3; ++j) if (graph & (1u<<(3*i+j))) expected |= 1u<<j;
    auto r = b.run(); for (unsigned i = 0; i < 3; ++i) CHECK_EQ(r.families[i].kept,(expected & (1u<<i)) != 0);
  }
}
TEST(prune_exhaustive_instantiation_trigger_closure) {
  // Reachability oracle: retain reverse-reachable triggers, then everything
  // needed by those triggers. Every directed three-family graph and seed set.
  for (unsigned graph = 0; graph < 512; ++graph) for (unsigned seeds = 0; seeds < 8; ++seeds) {
    Model b; bool reach[3][3]{};
    for (unsigned i = 0; i < 3; ++i) {
      b.add("f"+std::to_string(i),"void body(){}"); reach[i][i] = true;
      if (seeds & (1u<<i)) b.m.families[i].instantiationEffect = "effect";
    }
    for (unsigned i = 0; i < 3; ++i) for (unsigned j = 0; j < 3; ++j) if (graph & (1u<<(3*i+j))) {
      reach[i][j] = true; b.m.families[i].edges.push_back(j);
    }
    for (unsigned k = 0; k < 3; ++k) for (unsigned i = 0; i < 3; ++i) for (unsigned j = 0; j < 3; ++j)
      reach[i][j] |= reach[i][k] && reach[k][j];
    bool expected[3]{};
    for (unsigned trigger = 0; trigger < 3; ++trigger) for (unsigned seed = 0; seed < 3; ++seed)
      if ((seeds & (1u<<seed)) && reach[trigger][seed])
        for (unsigned needed = 0; needed < 3; ++needed) expected[needed] |= reach[trigger][needed];
    const auto r = b.run(); CHECK(r.disabledReasons.empty());
    for (unsigned i = 0; i < 3; ++i) CHECK_EQ(r.families[i].kept,expected[i]);
  }
}
TEST(prune_kinds_association_and_report_attribution) {
  Model b; auto key = b.add("Key","struct Key {};",FamilyKind::record); b.m.families[key].root = "explicit";
  auto hidden = b.add("Box","struct Box {};",FamilyKind::record); b.m.families[hidden].callableNames.insert("custom"); b.m.families[hidden].associatedTypes = {key};
  auto spec = b.add("hash","struct H {};",FamilyKind::specialization); b.m.families[spec].systemSpecialization = true; b.m.families[spec].associatedTypes = {key};
  auto assertion = b.add("assertion","static_assert(true);",FamilyKind::assertion);
  auto inst = b.add("inst","template struct T<int>;",FamilyKind::instantiation);
  auto r = b.run(); CHECK(r.families[hidden].kept); CHECK(r.families[spec].kept); CHECK(r.families[assertion].kept); CHECK(r.families[inst].kept);
  b.m.families[key].root.clear(); r = b.run(); CHECK(!r.families[hidden].kept); CHECK(!r.families[spec].kept);
}
TEST(prune_keep_metadata_before_comment_stripping) {
  const std::string text = "// distill: keep\nint retained();\nconst char* decoy=R\"(// distill: keep)\";\n/* // distill: keep */\nvoid ignored();\n";
  auto offsets = captureKeepOffsets(text); CHECK(offsets.ok()); CHECK_EQ(offsets.value().size(),1u);
  CHECK_EQ(text.substr(offsets.value().front(),3),"int");
  Model b; auto id = b.add("keep","int keep();"); b.m.annotations.push_back({0,id});
  CHECK(b.run().families[id].kept);
}

TEST(prune_location_intrinsics_require_renderer_and_do_not_excuse_pasting) {
  for (const auto* name : {"__LINE__","__FILE__"}) {
    Model b; b.add("dead","int dead;");
    PruneMacroExpansion e; e.name = name; e.predefined = true;
    e.parse = {MacroState::definedUnknown,{}, {FactOrigin::compilerBuiltin}};
    b.m.expansions = {e}; CHECK(b.run().disabledReasons.empty());
    b.m.judgeProfile = true; CHECK(!b.run().disabledReasons.empty());
    e.judge = {MacroState::definedUnknown,{}, {FactOrigin::specialBuiltin}};
    b.m.expansions = {e}; CHECK(b.run().disabledReasons.empty());
    e.paste = true; e.externalPasteOperand = true;
    b.m.expansions = {e}; CHECK(!b.run().disabledReasons.empty());
    e.paste = false; e.parse = known("42");
    b.m.expansions = {e}; CHECK(!b.run().disabledReasons.empty());
  }
}
