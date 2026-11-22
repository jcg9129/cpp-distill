// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/prune/extract.h"
#include "core/render/render.h"
#include "core/path.h"
#include "core/temporary.h"
#include <algorithm>
#include <fstream>
#include <iostream>
namespace {
using namespace distill;
template<class T> T take(Result<T> r) { if (!r.ok()) throw Error{r.error().message}; return std::move(r.value()); }
PruningModel extracted(const PruningModel& input, const PruneWorkerOptions& o) {
  auto m = take(extractPruning(input,o));
  // Print even for negative tests: an expected disable must never hide a
  // worker crash, protocol rejection or missing-header diagnostic in CI.
  for (const auto& why : m.disabledReasons) {
    std::cerr << "  disabled: " << why << '\n';
  }
  return m;
}
PruningModel extract(std::string text, const PruneWorkerOptions& o, bool assumeInstantiationEffects = true) {
  Configuration c; c.probe.triple = "x86_64-unknown-linux-gnu"; c.probe.dialect = {"c++20",false,202002};
  ResolveResult flat; flat.text = std::move(text);
  for (std::size_t i = 0, line = 1; i < flat.text.size(); ++i) if (i == 0 || flat.text[i-1] == '\n') flat.lineMap.push_back(SourceLocation{"original.hpp",line++});
  auto m = take(preparePruning(c,{},flat));
  m.assumeInstantiationEffects = assumeInstantiationEffects;
  return extracted(m,o);
}

}
int main(int argc, char** argv) {
  if (argc != 2) return 1;
  try {
    TemporaryDirectory temp(".prune-extract-tests-",std::filesystem::current_path());
    PruneWorkerOptions o; o.executable = std::filesystem::absolute(argv[1]); o.temporaryRoot = temp.path;
    std::size_t checks = 0, failed = 0, assertions = 0;
    std::string active, workerDiagnostics;
    o.runner = [&](const ProcessSpec& spec) {
      auto result = runProcess(spec);
      workerDiagnostics += result.ok() ? result.value().stderrText : result.error().message;
      return result;
    };
    PruningModel m; PruningResult r;
    auto check = [&](bool condition, const std::string& why) {
      ++assertions;
      if (!condition) { ++failed; std::cerr << "FAIL " << active << ": " << why << '\n'; }
    };
    auto family = [&](const PruningModel& model, std::string_view label) -> const PruneFamily& {
      for (const auto& f : model.families) if (f.label == label) return f;
      check(false,"missing family: " + std::string(label));
      static const PruneFamily missing;
      return missing;
    };
    auto kept = [&](std::string_view label) {
      for (FamilyId id = 0; id < m.families.size(); ++id) if (m.families[id].label == label) {
        if (id < r.families.size()) return r.families[id].kept;
        check(false,"retention unavailable: " + std::string(label)); return false;
      }
      check(false,"missing family: " + std::string(label)); return false;
    };
    auto macroFacts = [&] {
      std::string facts;
      for (const auto& x : m.expansions) {
        auto describe = [](const MacroFact& f) {
          static constexpr const char* states[] = {"undefined","known","definedUnknown","unknown"};
          std::string result = std::string("state=") + states[static_cast<int>(f.state)] +
              " replacement=[" + (f.definition ? f.definition->replacement : "<none>") + "] provenance=[";
          for (auto origin : f.provenance) result += std::to_string(static_cast<int>(origin)) + " ";
          return result + "]";
        };
        facts += "\n    " + x.name + "@" + std::to_string(x.offset) + " predefined=" + std::to_string(x.predefined) +
                 " paste=" + std::to_string(x.paste) + " external=" + std::to_string(x.externalPasteOperand) +
                 " parse{" + describe(x.parse) + "} judge{" + describe(x.judge) + "}";
      }
      return facts;
    };
    auto run = [&](const char* name, auto test) {
      active = name; m = {}; r = {}; workerDiagnostics.clear(); const auto before = failed;
      try { test(); }
      catch (const Error& e) { check(false,e.message); }
      catch (const std::exception& e) { check(false,e.what()); }
      for (const auto& why : m.disabledReasons)
        if (why.starts_with("pruning worker") || why.starts_with("invalid pruning worker result")) check(false,why);
      if (before == failed) { ++checks; std::cout << "PASS " << active << '\n'; }
      else {
        std::cerr << workerDiagnostics;
        for (const auto& why : m.disabledReasons) std::cerr << "  disabled: " << why << '\n';
        for (const auto& why : r.disabledReasons)
          if (std::find(m.disabledReasons.begin(),m.disabledReasons.end(),why) == m.disabledReasons.end())
            std::cerr << "  disabled: " << why << '\n';
        for (FamilyId id = 0; id < m.families.size(); ++id)
          std::cerr << "  " << m.families[id].label << ": "
                    << (id < r.families.size() ? r.families[id].reason : "retention unavailable") << '\n';
      }
    };
    run("cc1 translated language options", [&] {
      for (const auto* triple : {"x86_64-unknown-linux-gnu","aarch64-unknown-linux-gnu"}) {
        Configuration c; c.probe.triple = triple; c.probe.dialect = {"c++20",false,202002};
        c.command = take(parseCompileCommand({"g++","-funsigned-char","-fsigned-char","-fshort-wchar",
            "-fno-exceptions","-fno-rtti","-pthread","-O2","main.cpp"},temp.path));
        ResolveResult flat; flat.text =
            "static_assert(char(-1) < 0);\nstatic_assert(sizeof(wchar_t) == 2 && wchar_t(-1) > 0);\n"
            "#if defined(__EXCEPTIONS__) || defined(__GXX_RTTI) || !defined(_REENTRANT) || !defined(__OPTIMIZE__)\n"
            "#error wrong translated language options\n#endif\nint main(){}\n";
        m = extracted(take(preparePruning(c,{},flat)),o);
        check(m.disabledReasons.empty(),std::string("translated options rejected for ") + triple);
      }
    });
    run("cc1 unsigned default and ordered overrides", [&] {
      Configuration c; c.probe.triple = "aarch64-unknown-linux-gnu"; c.probe.dialect = {"c++20",false,202002};
      c.probe.macros["__CHAR_UNSIGNED__"] = Macro{false,{},false,"1"};
      c.command = take(parseCompileCommand({"g++","-pthread","-U_REENTRANT","-O2","-O0","main.cpp"},temp.path));
      ResolveResult flat; flat.text = "static_assert(char(-1) > 0);\n"
          "#if defined(_REENTRANT) || defined(__OPTIMIZE__)\n#error overrides lost\n#endif\nint main(){}\n";
      m = extracted(take(preparePruning(c,{},flat)),o);
      check(m.disabledReasons.empty(),"unsigned default or macro/optimization overrides rejected");
    });
    run("template ownership", [&] {
      m = extract("template<class T> struct Box { void f(); };\ntemplate<class T> void Box<T>::f() { int local=0; (void)local; }\nint unused() { return 0; }\nint main() { Box<int> x; x.f(); }\n",o);
      r = take(computeRetention(m)); check(r.disabledReasons.empty(),"ownership unexpectedly disabled");
      const auto& box = family(m,"Box"); check(!box.ranges.empty(),"missing template range");
      check(!box.ranges.empty() && m.text.substr(box.ranges[0].begin,8) == "template","template prefix missing");
      check(!box.ranges.empty() && box.ranges.back().end > 0 && m.text[box.ranges.back().end-1] == ';',"record terminator missing");
      check(!box.names.contains("local") && !box.names.contains("f"),"member/local indexed");
      check(!m.unions.empty(),"out-of-line member not aggregated");
    });
    // Inspect both retained and removed spellings, including every redeclaration.
    auto templateCase = [&](std::string text, const std::set<std::string>& live,
                            const std::set<std::string>& dead) {
      m = extract(std::move(text),o); r = take(computeRetention(m));
      check(r.disabledReasons.empty(),"template extraction disabled");
      std::set<std::string> found;
      std::vector<FamilyId> groups(m.families.size());
      for (FamilyId id = 0; id < groups.size(); ++id) groups[id] = id;
      auto group = [&](FamilyId id) { while (groups[id] != id) id = groups[id]; return id; };
      for (auto [a,b] : m.unions) groups[group(a)] = group(b);
      std::map<std::string,FamilyId> first;
      for (FamilyId id = 0; id < m.families.size(); ++id) {
        const auto& f = m.families[id];
        if (!live.contains(f.label) && !dead.contains(f.label)) continue;
        found.insert(f.label);
        const auto [it,fresh] = first.emplace(f.label,id);
        check(fresh || group(id) == group(it->second),"split template redeclarations/specializations: " + f.label);
        check(r.families[id].kept == live.contains(f.label),"wrong template retention: " + f.label);
        check(!f.ranges.empty(),"missing template spelling: " + f.label);
        for (auto range : f.ranges)
          check(m.text.substr(range.begin,8) == "template" || f.kind == FamilyKind::record,
                "template header not owned: " + f.label);
      }
      for (const auto& label : live) check(found.contains(label),"missing live template: " + label);
      for (const auto& label : dead) check(found.contains(label),"missing dead template: " + label);
      const auto out = take(renderSource(m,&r));
      take(renderingInvariant(m,&r,out.provenance.text));
      take(staticNameInvariant(m,r,out.provenance.text));
      return out.provenance.text;
    };
    run("function template forward declaration and definition", [&] {
      const auto out = templateCase(
          "template<class T> int forwarded();\n"
          "template<class T> int deadForward();\n"
          "int main(){return forwarded<int>()-3;}\n"
          "template<class T> int forwarded(){return 3;}\n"
          "template<class T> int deadForward(){return 7;}\n",
          {"forwarded"},{"deadForward"});
      const auto first = out.find("int forwarded");
      check(first != out.npos && out.find("int forwarded",first+1) != out.npos,"lost forward declaration or definition");
      check(out.find("deadForward") == out.npos,"dead redeclarations retained");
    });
    run("member template owns outer and inner headers", [&] {
      const auto out = templateCase(
          "template<class T> struct Live { template<class U> int f(U); };\n"
          "template<class T> template<class U> int Live<T>::f(U value){return value;}\n"
          "template<class T> struct Dead { template<class U> void f(U); };\n"
          "template<class T> template<class U> void Dead<T>::f(U){}\n"
          "template<class T> int forwarded();\n"
          "int main(){Live<int> x; return x.f(3)-forwarded<int>();}\n"
          "template<class T> int forwarded(){return 3;}\n",
          {"Live","Live::f","forwarded"},{"Dead","Dead::f"});
      check(out.find("template<class T> template<class U> int Live<T>::f") != out.npos,"outer header lost");
      check(out.find("Dead") == out.npos,"dead member template retained");
      bool ownsOuter = false;
      for (const auto& f : m.families) if (f.label == "Dead::f") for (auto range : f.ranges)
        ownsOuter |= std::string_view(m.text).substr(range.begin).starts_with("template<class T> template<class U>");
      check(ownsOuter,"outer header orphaned");
    });
    run("explicit specializations share template family", [&] {
      const auto out = templateCase(
          "template<class T> int chosen(){return 1;}\n"
          "template<> int chosen<int>(){return 3;}\n"
          "template<class T> struct Box { static int get(){return 1;} };\n"
          "template<> struct Box<int> { static int get(){return 3;} };\n"
          "template<class T> int deadSpecial(){return 1;}\n"
          "template<> int deadSpecial<int>(){return 2;}\n"
          "int main(){return chosen<int>()-Box<int>::get();}\n",
          {"chosen","Box"},{"deadSpecial"});
      check(out.find("template<> int chosen<int>") != out.npos,"specialization prefix lost");
      check(out.find("deadSpecial") == out.npos,"dead specialization retained");
    });
    run("friend template declaration and definition ownership", [&] {
      const auto out = templateCase(
          "struct LiveFriend { template<class T> friend int friendCall(T); };\n"
          "template<class T> int friendCall(T){return 3;}\n"
          "struct DeadFriend { template<class T> friend int deadFriend(T); };\n"
          "template<class T> int deadFriend(T){return 7;}\n"
          "int main(){LiveFriend x; (void)x; return friendCall(0)-3;}\n",
          {"LiveFriend","friendCall"},{"DeadFriend","deadFriend"});
      check(out.find("friend int friendCall") != out.npos,"friend declaration lost");
      check(out.find("deadFriend") == out.npos,"dead friend family retained");
    });
    run("namespace shells do not root contents", [&] {
      m = extract("namespace gone { namespace inner { int dead; } }\n"
                  "inline namespace version { int deadInline; }\n"
                  "namespace { int deadAnonymous; }\n"
                  "namespace viaUsing { int deadUsing; }\nusing namespace viaUsing;\n"
                  "namespace viaAlias { int deadAlias; }\nnamespace alias = viaAlias;\nusing namespace alias;\n"
                  "int main(){}\n",o);
      r = take(computeRetention(m));
      check(r.disabledReasons.empty(),"namespace extraction disabled");
      for (FamilyId id = 0; id < m.families.size(); ++id) {
        if (m.families[id].kind == FamilyKind::variable)
          check(!r.families[id].kept,"shell rooted variable " + m.families[id].label);
      }
      const auto out = take(renderSource(m,&r)).provenance.text;
      take(renderingInvariant(m,&r,out)); take(staticNameInvariant(m,r,out));
      check(out.find("namespace gone") == out.npos,"nested empty shell retained");
      check(out.find("namespace version") == out.npos,"inline empty shell retained");
      check(out.find("namespace {") == out.npos,"anonymous empty shell retained");
      check(out.find("namespace viaUsing") != out.npos,"using target shell removed");
      check(out.find("namespace viaAlias") != out.npos,"alias target shell removed");
    });
    run("shared declarators", [&] {
      m = extract("int a, b; struct S {} object; int main(){return b;}\n",o); r = take(computeRetention(m));
      check(r.disabledReasons.empty(),"declaration groups disabled");
      check(!m.unions.empty(),"shared declarators not joined");
      std::size_t bytes = 0; for (const auto& f : m.families) for (auto range : f.ranges) bytes += range.end-range.begin;
      check(bytes <= m.text.size(),"declaration bytes counted twice");
    });
    run("overloads", [&] {
      m = extract("namespace n { int f(int){return 0;} int f(long){return 0;} } int main(){return n::f(1);}\n",o);
      r = take(computeRetention(m)); check(r.disabledReasons.empty(),"overloads disabled");
      check(!m.unions.empty(),"overloads not aggregated");
    });
    run("storage", [&] {
      m = extract("constexpr int get_value(){return 1;} int active=get_value(); void automatic(){int x=get_value();(void)x;} void stat(){static int x=get_value();} int main(){}\n",o);
      r = take(computeRetention(m)); check(r.disabledReasons.empty(),"storage disabled");
      check(!family(m,"active").root.empty(),"constexpr call incorrectly inert");
      check(family(m,"automatic").root.empty(),"automatic local rooted");
      check(!family(m,"stat").root.empty(),"static local not rooted");
    });
    run("instantiation effect patterns include nested storage and attributes", [&] {
      for (const auto& source : {
          "int notify(); template<class T> struct Effect { static inline int x = notify(); };",
          "int notify(); template<class T> inline int Effect = notify();",
          "template<class T> __attribute__((used)) void Effect() {}"}) {
        m = extract(std::string(source) + "\nstruct Dead {}; int main(){}\n",o);
        r = take(computeRetention(m)); check(r.disabledReasons.empty(),"effect pattern disabled");
        check(std::any_of(m.families.begin(),m.families.end(),[](const auto& f) { return f.label.starts_with("Effect") && !f.instantiationEffect.empty(); }),"effect pattern not classified: " + std::string(source));
        check(!kept("Dead"),"effect pattern disabled all deletion");
      }
    });
    run("D3e dynamic block storage keeps roots but releases unrelated triggers", [&] {
      for (const auto* source : {
          "int notify(); template<class T> int Effect() { static int x = notify(); return x; } int dormant(){ return Effect<int>(); }",
          "int notify(); template<class T> struct Effect { int f() { static thread_local int x = notify(); return x; } }; int dormant(){ return Effect<int>{}.f(); }",
          "int notify(); template<class T> struct Effect { int f(); }; template<class T> int Effect<T>::f() { static int x = notify(); return x; } int dormant(){ return Effect<int>{}.f(); }"}) {
        m = extract(std::string(source) + "\nint main(){}\n",o);
        r = take(computeRetention(m)); check(r.disabledReasons.empty(),"dynamic local disabled");
        check(!kept("dormant"),"unrelated trigger retained for dynamic block local");
        check(std::any_of(m.families.begin(),m.families.end(),[](const auto& f) {
          return f.label.starts_with("Effect") && !f.root.empty() && f.instantiationEffect.empty();
        }),"dynamic local changed rule-1 root or remained a source");
      }
    });
    run("D3e non-constexpr constant storage releases triggers", [&] {
      for (const auto* source : {
          "using Scalar=unsigned; template<int> inline const Scalar value=3;",
          "struct Plain { int x; }; template<int> inline const Plain value{3};",
          "struct Base { int x; }; struct Plain:Base { int y; }; template<int> inline const Plain value{{3},4};",
          "using Scalar=unsigned; template<int> inline const Scalar value[2]={3,4};",
          "template<int> inline const int* value=nullptr;"}) {
        m = extract(std::string(source) + "\nvoid dormant(){ (void)&value<0>; } int main(){}\n",o);
        r = take(computeRetention(m)); check(r.disabledReasons.empty(),"constant storage disabled");
        check(family(m,"value").instantiationEffect.empty(),"fixed constant storage remains a source");
        check(!kept("dormant"),"constant storage retained unrelated trigger");
      }
    });
    run("A11 retains non-constexpr uncertain non-block storage", [&] {
      for (const auto* source : {
          "int notify(); template<int> inline int value=notify();",
          "template<int N, class T=int> inline T value{};",
          "struct Noisy { constexpr Noisy(){} ~Noisy(){} }; template<int> inline const Noisy value{};",
          "template<bool> struct Pick { using type=int; }; template<> struct Pick<true> { struct type { constexpr ~type(){} }; }; template<int> inline const Pick<(sizeof(long)==4)>::type value{};",
          "template<bool> struct Pick { using type=int; }; template<> struct Pick<true> { struct type { constexpr ~type(){} }; }; struct Plain { Pick<(sizeof(long)==4)>::type field; }; template<int> inline const Plain value{};",
          "struct Noisy { constexpr ~Noisy(){} }; struct Holder { const Noisy& member; }; template<int> inline const Holder value{Noisy{}};",
          "struct Noisy { constexpr ~Noisy(){} }; struct Holder { const Noisy& member=Noisy{}; }; template<int> inline const Holder value{};"}) {
        m = extract(std::string(source) + "\nvoid dormant(){ (void)&value<0>; } int main(){}\n",o);
        r = take(computeRetention(m)); check(r.disabledReasons.empty(),"conservative storage disabled");
        check(!family(m,"value").instantiationEffect.empty(),"uncertain storage incorrectly exempted: " + std::string(source));
        check(kept("dormant"),"possible storage trigger removed: " + std::string(source));
      }
    });
    run("A11 excludes all block static and thread storage effects", [&] {
      for (const auto* source : {
          "int notify(); template<int> void value(){ static int object=sizeof(long)==4 ? 0 : notify(); }",
          "int notify(unsigned long); constexpr int notify(unsigned){return 0;} template<int> void value(){ static int object=notify(sizeof(long)); }",
          "struct Noisy { constexpr Noisy(){} ~Noisy(){} }; template<int> void value(){ static Noisy const object; }",
          "template<int N, class T=int> void value(){ static T const object{}; }",
          "template<int N, class T=int> void value(){ thread_local T const object{}; }",
          "struct Noisy { ~Noisy(){} }; template<int> void value(){ static const Noisy& object=Noisy{}; }",
          "struct Noisy { ~Noisy(){} }; template<int> void value(){ thread_local const Noisy& object=Noisy{}; }"}) {
        m = extract(std::string(source) + "\nvoid dormant(){ (void)&value<0>; } int main(){}\n",o);
        r = take(computeRetention(m)); check(r.disabledReasons.empty(),"block A11 disabled");
        check(!family(m,"value").root.empty(),"block A11 lost rule-1 root");
        check(family(m,"value").instantiationEffect.empty(),"block A11 remains a source");
        check(!kept("dormant"),"block A11 retained unrelated trigger");
        check(r.assumptions.contains("A11"),"block A11 not reported");
      }
    });
    run("A11 excludes constexpr storage of any type and extended objects", [&] {
      for (const auto* source : {
          "struct Quiet { int x=0; }; template<int> constexpr const int& value=Quiet{}.x;",
          "struct Quiet { int x=0; }; struct Holder { const Quiet& member; }; template<int> constexpr Holder value{Quiet{}};",
          "struct Quiet { int x=0; }; struct Holder { const Quiet& member=Quiet{}; }; template<int> constexpr Holder value{};",
          "template<bool> struct Pick { struct type { int x=0; }; }; template<> struct Pick<true> { struct type { int x=0; constexpr ~type(){} }; }; template<int> constexpr const int& value=Pick<(sizeof(long)==4)>::type{}.x;",
          "struct Noisy { constexpr ~Noisy(){} }; template<int> void value(){ static constexpr Noisy object{}; }",
          "template<bool> struct Pick { using type=int; }; template<> struct Pick<true> { struct type { constexpr ~type(){} }; }; template<int> constexpr Pick<(sizeof(long)==4)>::type value{};",
          "template<bool> struct Pick { using type=int; }; template<> struct Pick<true> { struct type { constexpr ~type(){} }; }; struct Plain { Pick<(sizeof(long)==4)>::type field; }; template<int> constexpr Plain value{};",
          "template<int N, class T=int> constexpr T value{};",
          "template<int> constexpr auto value=0;",
          "template<int N, class T=int> void value(){ static thread_local constexpr T object{}; }",
          "template<int N, class T=int> void value(){ static constexpr T object{}; }"}) {
        m = extract(std::string(source) + "\nvoid dormant(){ (void)&value<0>; } int main(){}\n",o);
        r = take(computeRetention(m)); check(r.disabledReasons.empty(),"A11 storage disabled");
        check(family(m,"value").instantiationEffect.empty(),"A11 constexpr storage remains a source");
        check(!kept("dormant"),"A11 constexpr storage retained unrelated trigger");
        check(r.assumptions.contains("A11"),"A11 not reported");
      }
    });
    run("A11 exemptions preserve other effects in the same family", [&] {
      for (const auto* source : {
          "int notify(); template<int> struct Effect { static inline int live=notify(); static constexpr int quiet=0; void local(){static int x=notify();} };",
          "int notify(); template<int> struct Effect { void local(){static int x=notify();} static constexpr int quiet=0; static inline int live=notify(); };",
          "template<int> __attribute__((used)) void Effect(){static constexpr int quiet=0;}"}) {
        const bool function = std::string_view(source).find("__attribute__") != std::string_view::npos;
        m = extract(std::string(source) + (function ? "\nvoid dormant(){ (void)&Effect<0>; } int main(){}\n" :
            "\nvoid dormant(){ (void)sizeof(Effect<0>); } int main(){}\n"),o);
        r = take(computeRetention(m)); check(r.disabledReasons.empty(),"mixed effects disabled");
        check(!family(m,"Effect").instantiationEffect.empty(),"A11 exemption cleared another effect");
        check(kept("dormant"),"mixed effect lost its trigger");
      }
    });
    run("A11 excludes friend-definition instantiation", [&] {
      for (const auto* source : {
          "template<class T> struct Effect { friend auto supplied(T) { return 7; } };",
          "struct Effect { template<class T> struct Nested { friend auto supplied(T) { return 7; } }; };",
          "template<class T> struct Effect {}; template<class T> struct Effect<T*> { friend auto supplied(T) { return 7; } };",
          "template<class T> struct Effect { friend int supplied(T); };"}) {
        m = extract(std::string(source) + "\nint main(){}\n",o);
        r = take(computeRetention(m)); check(r.disabledReasons.empty(),"friend exclusion disabled");
        check(family(m,"Effect").instantiationEffect.empty(),"A11 friend remains an effect source");
        check(!kept("Effect"),"A11 friend pattern retained");
        check(r.assumptions.contains("A11"),"A11 not reported");
      }
    });
    run("static storage scalar aliases preserve written type inspection", [&] {
      const auto dir = temp.path / "scalar-system";
      std::filesystem::create_directories(dir);
      { std::ofstream h(dir / "cstdint"); h << "using uint32_t=unsigned; namespace std { using ::uint32_t; }\n"; }
      Configuration c; c.probe.triple = "x86_64-unknown-linux-gnu"; c.probe.dialect = {"c++20",false,202002};
      SearchDirectory d; d.path = dir; d.kind = IncludeKind::system; c.probe.searchLists.directories.push_back(d);
      ResolveResult flat; flat.text =
          "#include <cstdint>\nusing U=std::uint32_t; template<class T> struct Inert { static inline U x=0; };\n"
          "template<bool B> struct Pick { using type=int; }; template<> struct Pick<false> { struct type {}; };\n"
          "template<class T> struct Selected { static inline Pick<(sizeof(long)>4)>::type x={}; };\n"
          "int main(){}\n";
      for (std::size_t i = 0, line = 1; i < flat.text.size(); ++i) if (i == 0 || flat.text[i-1] == '\n') flat.lineMap.push_back(SourceLocation{"original.hpp",line++});
      m = extracted(take(preparePruning(c,{},flat)),o);
      r = take(computeRetention(m)); check(r.disabledReasons.empty(),"scalar alias inspection disabled");
      check(family(m,"Inert").instantiationEffect.empty(),"standard scalar UsingType incorrectly effectful");
      check(!kept("Inert"),"inert scalar alias retained");
      check(!family(m,"Selected").instantiationEffect.empty(),"target-selected member type incorrectly inert");
      check(kept("Selected"),"potential class storage removed");
    });
    run("hidden hook carrier certificate uses written own record parameters", [&] {
      m = extract("int notify(); template<class T> struct Reg { static inline int x=notify(); };\n"
                  "template<class T> struct S { friend bool operator==(S,S) { return Reg<T>::x==0; } };\n"
                  "struct Unused { decltype(sizeof(S<int>)) f(); }; int main(){}\n",o);
      r = take(computeRetention(m)); check(r.disabledReasons.empty(),"self-associated hidden friend refused");
      check(family(m,"S").boundedSystemHooks,"direct self parameter not certified");
      check(family(m,"S").instantiationEffect.empty(),"A11 friend seeded the reverse closure");
      check(!family(m,"Reg").instantiationEffect.empty(),"storage source missing");
      check(kept("Unused"),"conservative hidden friend trigger removed");
      m = extract("int notify(); template<class T> struct Reg { static inline int x=notify(); };\n"
                  "template<class T> struct S { using Self=S; friend bool operator==(Self,Self) { return Reg<T>::x==0; } };\n"
                  "int main(){}\n",o);
      r = take(computeRetention(m));
      check(!family(m,"S").boundedSystemHooks,"alias incorrectly certified a carrier");
      check(std::any_of(r.disabledReasons.begin(),r.disabledReasons.end(),[](const auto& why) {
        return why.starts_with("unbounded system instantiation effects through");
      }),"uncertified hidden hook did not refuse");
    });
    run("inert template storage and ordinary default initializers have no intrinsic effect", [&] {
      m = extract("template<class T> struct Inert { static constexpr int x = sizeof(T); int y = 3; };\n"
                  "template<class T> int ordinary() { static int x = 0; return x; }\n"
                  "template<int N> struct Simple {};\n"
                  "int main(){}\n",o);
      r = take(computeRetention(m)); check(r.disabledReasons.empty(),"inert controls disabled");
      for (const auto* label : {"Inert","ordinary","Simple"}) {
        check(family(m,label).instantiationEffect.empty(),"inert pattern classified effectful");
        check(!kept(label),"inert pattern retained");
      }
    });
    run("nonclass NTTPs and proven trivial parameter objects are not storage effects", [&] {
      for (const auto* source : {
          "using U=unsigned; template<U N> struct Candidate {};",
          "template<class T> using Identity=T; template<Identity<unsigned> N> struct Candidate {};",
          "using uint32_t=unsigned; namespace std { using ::uint32_t; } using U=std::uint32_t; template<U N> struct Candidate {};",
          "enum class Mode { quick }; template<Mode M> struct Candidate {};",
          "struct V { constexpr ~V(){} }; template<V* P> struct Candidate {};",
          "struct V { constexpr ~V(){} }; template<V& P> struct Candidate {};",
          "template<class T, T* P> struct Candidate {};",
          "template<class T, T& P> struct Candidate {};",
          "template<auto* P> struct Candidate {};",
          "template<auto& P> struct Candidate {};",
          "struct V { int x[2]; }; template<V P> struct Candidate {};",
          "struct Base {}; struct V:Base { int x; }; template<V P> struct Candidate {};",
          "template<unsigned N> void Candidate() { []<unsigned P>(){}; }"}) {
        m = extract(std::string(source) + "\nint main(){}\n",o);
        r = take(computeRetention(m)); check(r.disabledReasons.empty(),"NTTP exemption disabled");
        check(family(m,"Candidate").instantiationEffect.empty(),"nonclass/trivial NTTP classified effectful: " + std::string(source));
        check(!kept("Candidate"),"inert NTTP retained: " + std::string(source));
      }
    });
    run("A11 excludes unknown and nontrivial NTTP object destruction", [&] {
      for (const auto* source : {
          "template<auto P> struct Candidate {};",
          "template<decltype(auto) P> struct Candidate {};",
          "template<class T> concept Any=true; template<Any auto P> struct Candidate {};",
          "template<class T, T P> struct Candidate {};",
          "template<class T, typename T::type P> struct Candidate {};",
          "template<bool B> struct Pick { using type=int; }; template<> struct Pick<false> { struct type { constexpr ~type(){} }; }; template<Pick<(sizeof(long)>4)>::type P> struct Candidate {};",
          "template<bool B> struct Pick { enum class type { zero }; }; template<> struct Pick<false> { struct type { constexpr ~type(){} }; }; template<Pick<(sizeof(long)>4)>::type P> struct Candidate {};",
          "struct V { constexpr ~V(){} }; using Alias=V; template<Alias P> struct Candidate {};"}) {
        m = extract(std::string(source) + "\nint main(){}\n",o);
        r = take(computeRetention(m)); check(r.disabledReasons.empty(),"NTTP control disabled");
        check(family(m,"Candidate").instantiationEffect.empty(),"A11 NTTP still classified effectful: " + std::string(source));
        check(!kept("Candidate"),"A11 NTTP retained");
        check(r.assumptions.contains("A11"),"A11 not reported");
      }
    });
    run("without instantiation-effects: instantiation effect patterns include nested storage and friends", [&] {
      for (const auto& source : {
          "template<class T> struct Effect { friend auto supplied(T) { return 7; } };",
          "struct Effect { template<class T> struct Nested { friend auto supplied(T) { return 7; } }; };",
          "template<class T> struct Effect {}; template<class T> struct Effect<T*> { friend auto supplied(T) { return 7; } };",
          "int notify(); template<class T> struct Effect { static inline int x = notify(); };",
          "int notify(); template<class T> inline int Effect = notify();",
          "template<class T> __attribute__((used)) void Effect() {}",
          "template<auto V> struct Effect {};",
          "struct Value { constexpr ~Value(){} }; template<Value V> struct Effect {};",
          "int notify(); template<class T> int Effect() { static int x = notify(); return x; }",
          "int notify(); template<class T> struct Effect { int f() { static int x = notify(); return x; } };",
          "int notify(); template<class T> struct Effect { int f(); }; template<class T> int Effect<T>::f() { static int x = notify(); return x; }"}) {
        m = extract(std::string(source) + "\nstruct Dead {}; int main(){}\n",o,false);
        r = take(computeRetention(m)); check(r.disabledReasons.empty(),"effect pattern disabled");
        check(std::any_of(m.families.begin(),m.families.end(),[](const auto& f) { return f.label.starts_with("Effect") && !f.instantiationEffect.empty(); }),"effect pattern not classified: " + std::string(source));
        check(!kept("Dead"),"effect pattern disabled all deletion");
      }
    });
    run("without instantiation-effects: hidden hook carrier certificate uses written own record parameters", [&] {
      m = extract("template<class T> struct S { friend bool operator==(S,S) { return true; } };\n"
                  "struct Unused { decltype(sizeof(S<int>)) f(); }; int main(){}\n",o,false);
      r = take(computeRetention(m)); check(r.disabledReasons.empty(),"self-associated hidden friend refused");
      check(family(m,"S").boundedSystemHooks,"direct self parameter not certified");
      check(kept("Unused"),"conservative hidden friend trigger removed");
      m = extract("template<class T> struct S { using Self=S; friend bool operator==(Self,Self) { return true; } };\n"
                  "int main(){}\n",o,false);
      r = take(computeRetention(m));
      check(!family(m,"S").boundedSystemHooks,"alias incorrectly certified a carrier");
      check(std::any_of(r.disabledReasons.begin(),r.disabledReasons.end(),[](const auto& why) {
        return why.starts_with("unbounded system instantiation effects through");
      }),"uncertified hidden hook did not refuse");
    });
    run("without instantiation-effects: nonclass NTTPs and proven trivial parameter objects are not storage effects", [&] {
      for (const auto* source : {
          "using U=unsigned; template<U N> struct Candidate {};",
          "template<class T> using Identity=T; template<Identity<unsigned> N> struct Candidate {};",
          "using uint32_t=unsigned; namespace std { using ::uint32_t; } using U=std::uint32_t; template<U N> struct Candidate {};",
          "enum class Mode { quick }; template<Mode M> struct Candidate {};",
          "struct V { constexpr ~V(){} }; template<V* P> struct Candidate {};",
          "struct V { constexpr ~V(){} }; template<V& P> struct Candidate {};",
          "template<class T, T* P> struct Candidate {};",
          "template<class T, T& P> struct Candidate {};",
          "template<auto* P> struct Candidate {};",
          "template<auto& P> struct Candidate {};",
          "struct V { int x[2]; }; template<V P> struct Candidate {};",
          "struct Base {}; struct V:Base { int x; }; template<V P> struct Candidate {};",
          "template<unsigned N> void Candidate() { []<unsigned P>(){}; }"}) {
        m = extract(std::string(source) + "\nint main(){}\n",o,false);
        r = take(computeRetention(m)); check(r.disabledReasons.empty(),"NTTP exemption disabled");
        check(family(m,"Candidate").instantiationEffect.empty(),"nonclass/trivial NTTP classified effectful: " + std::string(source));
        check(!kept("Candidate"),"inert NTTP retained: " + std::string(source));
      }
    });
    run("without instantiation-effects: unknown NTTP bindings remain conservative", [&] {
      for (const auto* source : {
          "template<auto P> struct Candidate {};",
          "template<decltype(auto) P> struct Candidate {};",
          "template<class T> concept Any=true; template<Any auto P> struct Candidate {};",
          "template<class T, T P> struct Candidate {};",
          "template<class T, typename T::type P> struct Candidate {};",
          "template<bool B> struct Pick { using type=int; }; template<> struct Pick<false> { struct type { constexpr ~type(){} }; }; template<Pick<(sizeof(long)>4)>::type P> struct Candidate {};",
          "template<bool B> struct Pick { enum class type { zero }; }; template<> struct Pick<false> { struct type { constexpr ~type(){} }; }; template<Pick<(sizeof(long)>4)>::type P> struct Candidate {};",
          "struct V { constexpr ~V(){} }; using Alias=V; template<Alias P> struct Candidate {};"}) {
        m = extract(std::string(source) + "\nint main(){}\n",o,false);
        r = take(computeRetention(m)); check(r.disabledReasons.empty(),"NTTP control disabled");
        check(!family(m,"Candidate").instantiationEffect.empty(),"possible class binding lost: " + std::string(source));
        check(kept("Candidate"),"unknown NTTP removed");
      }
    });
    run("without instantiation-effects: block storage sources return", [&] {
      for (const auto* source : {
          "int notify(); template<int> void value(){ static int object=sizeof(long)==4 ? 0 : notify(); }",
          "int notify(unsigned long); constexpr int notify(unsigned){return 0;} template<int> void value(){ static int object=notify(sizeof(long)); }",
          "struct Noisy { constexpr Noisy(){} ~Noisy(){} }; template<int> void value(){ static Noisy const object; }",
          "template<int N, class T=int> void value(){ static T const object{}; }",
          "template<int N, class T=int> void value(){ thread_local T const object{}; }",
          "struct Noisy { ~Noisy(){} }; template<int> void value(){ static const Noisy& object=Noisy{}; }",
          "struct Noisy { ~Noisy(){} }; template<int> void value(){ thread_local const Noisy& object=Noisy{}; }"}) {
        m = extract(std::string(source) + "\nvoid dormant(){ (void)&value<0>; } int main(){}\n",o,false);
        r = take(computeRetention(m)); check(r.disabledReasons.empty(),"flag-mode block storage disabled");
        check(!family(m,"value").root.empty(),"flag-mode block storage lost rule-1 root");
        check(!family(m,"value").instantiationEffect.empty(),"block storage lost effect source");
        check(kept("dormant"),"block storage lost potential trigger");
        check(!r.assumptions.contains("A11"),"disabled A11 was reported");
      }
    });
    run("without instantiation-effects: constexpr storage and extended objects return", [&] {
      for (const auto* source : {
          "struct Quiet { int x=0; }; template<int> constexpr const int& value=Quiet{}.x;",
          "struct Quiet { int x=0; }; struct Holder { const Quiet& member; }; template<int> constexpr Holder value{Quiet{}};",
          "struct Quiet { int x=0; }; struct Holder { const Quiet& member=Quiet{}; }; template<int> constexpr Holder value{};",
          "template<bool> struct Pick { struct type { int x=0; }; }; template<> struct Pick<true> { struct type { int x=0; constexpr ~type(){} }; }; template<int> constexpr const int& value=Pick<(sizeof(long)==4)>::type{}.x;",
          "struct Noisy { constexpr ~Noisy(){} }; template<int> void value(){ static constexpr Noisy object{}; }",
          "template<bool> struct Pick { using type=int; }; template<> struct Pick<true> { struct type { constexpr ~type(){} }; }; template<int> constexpr Pick<(sizeof(long)==4)>::type value{};",
          "template<bool> struct Pick { using type=int; }; template<> struct Pick<true> { struct type { constexpr ~type(){} }; }; struct Plain { Pick<(sizeof(long)==4)>::type field; }; template<int> constexpr Plain value{};",
          "template<int N, class T=int> constexpr T value{};",
          "template<int> constexpr auto value=0;",
          "template<int N, class T=int> void value(){ static thread_local constexpr T object{}; }",
          "template<int N, class T=int> void value(){ static constexpr T object{}; }"}) {
        m = extract(std::string(source) + "\nvoid dormant(){ (void)&value<0>; } int main(){}\n",o,false);
        r = take(computeRetention(m)); check(r.disabledReasons.empty(),"flag-mode constexpr storage disabled");
        check(!family(m,"value").instantiationEffect.empty(),"constexpr storage lost effect source");
        check(kept("dormant"),"constexpr storage lost potential trigger");
        check(!r.assumptions.contains("A11"),"disabled A11 was reported");
      }
    });
    run("without instantiation-effects: lifetime-safe constant storage releases triggers", [&] {
      for (const auto* source : {
          "using Scalar=unsigned; template<int> inline const Scalar value=3;",
          "struct Plain { int x; }; template<int> inline const Plain value{3};",
          "struct Base { int x; }; struct Plain:Base { int y; }; template<int> inline const Plain value{{3},4};",
          "using Scalar=unsigned; template<int> inline const Scalar value[2]={3,4};",
          "template<int> inline const int* value=nullptr;",
          "struct Plain { int x; }; template<int> constexpr Plain value{3};",
          "struct Plain { int x; }; template<int> void value(){ static const Plain object{3}; }",
          "struct Plain { int x; }; template<int> void value(){ thread_local const Plain object{3}; }"}) {
        m = extract(std::string(source) + "\nvoid dormant(){ (void)&value<0>; } int main(){}\n",o,false);
        r = take(computeRetention(m)); check(r.disabledReasons.empty(),"constant storage disabled");
        check(family(m,"value").instantiationEffect.empty(),"fixed constant storage remains a source");
        check(!kept("dormant"),"constant storage retained unrelated trigger");
      }
    });
    run("constexpr arithmetic template members", [&] {
      m = extract("template<class T> constexpr bool scalar = false;\n"
                  "template<class T> struct Unused { static constexpr bool x = !scalar<T>; };\n"
                  "template<class T> struct Used { static constexpr bool x = !scalar<T>; };\n"
                  "int main(){return Used<int>::x ? 0 : 1;}\n",o);
      r = take(computeRetention(m)); check(r.disabledReasons.empty(),"constexpr templates disabled");
      check(!kept("Unused"),"unused constexpr bool member rooted its record");
      check(kept("Used") && kept("scalar"),"used member or initializer dependency removed");
      check(family(m,"Unused").root.empty(),"unused pattern still a storage root");
      check(r.assumptions.contains("A7"),"A7 not reported");
      check(r.assumptions.contains("A11"),"A11 not reported");
      const auto out = take(renderSource(m,&r)).provenance.text;
      take(renderingInvariant(m,&r,out)); take(staticNameInvariant(m,r,out));
      check(out.find("struct Unused") == out.npos && out.find("struct Used") != out.npos,"template removal/retention failed");
    });
    run("constexpr empty tags", [&] {
      m = extract("struct UnusedTag {}; inline constexpr UnusedTag unused_tag{};\n"
                  "namespace n { struct UsedTag {}; } inline constexpr n::UsedTag used_tag = {};\n"
                  "int main(){return &used_tag == nullptr;}\n",o);
      r = take(computeRetention(m)); check(r.disabledReasons.empty(),"empty tags disabled");
      check(!kept("UnusedTag") && !kept("unused_tag"),"unused tag/type retained");
      check(kept("n::UsedTag") && kept("used_tag"),"referenced tag/type removed");
      const auto out = take(renderSource(m,&r)).provenance.text;
      take(renderingInvariant(m,&r,out)); take(staticNameInvariant(m,r,out));
      check(out.find("UnusedTag") == out.npos && out.find("n::UsedTag used_tag = {}") != out.npos,"tag removal/retention failed");
    });
    run("constexpr calls conversions and local static", [&] {
      m = extract("struct Convert { constexpr operator int() const { return 7; } constexpr ~Convert(){} };\n"
                  "constexpr int number(){return 7;}\n"
                  "constexpr int unused_number=number(); constexpr int converted=Convert{};\n"
                  "constexpr double floating=number(); constexpr int used_number=number();\n"
                  "void unused_function(){static constexpr int local=number();}\n"
                  "int main(){return used_number-7;}\n",o);
      r = take(computeRetention(m)); check(r.disabledReasons.empty(),"constexpr scalar calls disabled");
      for (const auto* name : {"unused_number","converted","floating","unused_function","Convert"})
        check(!kept(name),std::string("unused constexpr computation retained: ")+name);
      check(kept("used_number") && kept("number"),"referenced initializer closure removed");
    });
    for (const auto& [name,declaration] : std::vector<std::pair<const char*,const char*>>{
        {"alias", "using Alias=int; constexpr Alias candidate=number();"},
        {"decltype", "constexpr decltype(number()) candidate=number();"},
        {"auto", "constexpr auto candidate=number();"},
        {"constinit", "constinit int candidate=number();"},
        {"thread_local", "thread_local constexpr int candidate=number();"},
        {"volatile", "constexpr volatile int candidate=number();"},
        {"reference", "constexpr int backing=7; constexpr const int& candidate=backing;"},
        {"nonempty class", "struct Tag { int n; }; constexpr Tag candidate{};"},
        {"destructor", "struct Tag { constexpr ~Tag(){} }; constexpr Tag candidate{};"},
        {"defaulted destructor", "struct Tag { ~Tag()=default; }; constexpr Tag candidate{};"},
        {"constructor", "struct Tag { constexpr Tag(int=0){} }; constexpr Tag candidate{};"},
        {"base", "struct Base {}; struct Tag:Base {}; constexpr Tag candidate{};"},
        {"tag alias", "struct Tag {}; using Alias=Tag; constexpr Alias candidate{};"},
        {"tag decltype", "struct Tag {}; constexpr decltype(Tag{}) candidate{};"},
        {"tag auto", "struct Tag {}; constexpr auto candidate=Tag{};"},
        {"tag call", "struct Tag {}; constexpr Tag make(){return {};} constexpr Tag candidate=make();"},
        {"tag specialization", "template<class T> struct Tag {}; constexpr Tag<int> candidate{};"},
        {"tag attribute", "struct alignas(8) Tag {}; constexpr Tag candidate{};"},
        {"variable attribute", "[[maybe_unused]] constexpr int candidate=number();"},
        {"macro alias", "using Alias=int;\n#define TYPE Alias\nconstexpr TYPE candidate=number();"},
        {"macro decltype", "#define TYPE decltype(number())\nconstexpr TYPE candidate=number();"}}) {
      run((std::string("constexpr narrow retains ") + name).c_str(), [&] {
        m = extract(std::string("constexpr int number(){return 7;}\n") + declaration +
                    "\nint unrelated(){return 9;} int main(){}\n",o);
        r = take(computeRetention(m));
        if (std::string_view(name) == "constinit") {
          // Clang models constinit as ConstInitAttr: the attribute gate already
          // disables pruning, which is stricter than keeping a storage root.
          check(std::any_of(r.disabledReasons.begin(),r.disabledReasons.end(),[](const auto& why) {
            return why.starts_with("unsupported attribute: constinit"); }),"constinit not refused");
          return;
        }
        check(r.disabledReasons.empty(),"negative unexpectedly disabled");
        check(kept("candidate") && !family(m,"candidate").root.empty(),"excluded variable lost storage root");
        check(!kept("unrelated"),"negative did not allow unrelated removal");
      });
    }
    run("constexpr template parameter types stay written", [&] {
      m = extract("template<class T> struct Parameter { static constexpr T x=7; };\n"
                  "template<class T> struct Instantiated { static constexpr T x=7; };\n"
                  "int main(){return Instantiated<int>::x-7;}\n",o);
      r = take(computeRetention(m)); check(r.disabledReasons.empty(),"parameter-type control disabled");
      check(kept("Parameter") && !family(m,"Parameter").root.empty(),"parameter-typed pattern got exemption");
      check(kept("Instantiated") && !family(m,"Instantiated").root.empty(),"substituted type got exemption");
    });
    run("constexpr specialization classified independently", [&] {
      m = extract("struct Noisy { constexpr Noisy(){} ~Noisy(){} };\n"
                  "template<class T> constexpr bool flag=true;\n"
                  "template<> Noisy flag<int>{};\n"
                  "template<class T> struct Box { static constexpr bool x=true; };\n"
                  "template<> struct Box<int> { inline static Noisy x{}; };\n"
                  "int unrelated(){return 9;} int main(){}\n",o);
      r = take(computeRetention(m)); check(r.disabledReasons.empty(),"specialization disabled");
      bool specializedRoot = false;
      for (FamilyId id=0; id<m.families.size(); ++id) {
        const auto& f = m.families[id];
        if (f.names.contains("flag") || f.names.contains("Box")) {
          check(r.families[id].kept,"specialization family removed");
          specializedRoot |= !f.root.empty();
        }
      }
      check(specializedRoot && kept("Noisy"),"specialization type/destructor root lost");
      check(!kept("unrelated"),"unrelated function retained");
      const auto out = take(renderSource(m,&r)).provenance.text;
      check(out.find("Noisy flag<int>{}") != out.npos && out.find("inline static Noisy x{}") != out.npos,"different-type specialization removed");
    });
    run("constexpr builtin macro type keeps other gates", [&] {
      m = extract("#define TYPE bool\nconstexpr int number(){return 7;}\n"
                  "constexpr TYPE candidate=number(); int main(){}\n",o);
      r = take(computeRetention(m)); check(r.disabledReasons.empty(),"user type macro disabled");
      check(family(m,"candidate").root.empty(),"expanded builtin type did not qualify");
      // Macro ownership/pinning may independently keep the declaration.
    });
    run("constexpr used attribute remains root", [&] {
      m = extract("constexpr int number(){return 7;}\n"
                  "[[gnu::used]] constexpr int candidate=number(); int main(){}\n",o);
      r = take(computeRetention(m)); check(r.disabledReasons.empty(),"used attribute disabled");
      check(kept("candidate") && !family(m,"candidate").root.empty(),"attribute root erased");
    });
    run("user paste", [&] {
      m = extract("#define JOIN_I(a,b) a##b\n#define JOIN(a,b) JOIN_I(a,b)\ntemplate<class T> void run(T x){JOIN(custo,mize)(x);}\nnamespace later { template<class T> void customize(T){} }\nint main(){}\n",o);
      r = take(computeRetention(m)); check(r.disabledReasons.empty(),"user-only paste disabled");
      check(family(m,"run").dependentNames.contains("customize"),"synthesized dependent name missing");
    });
    run("source macro override", [&] {
      m = extract("#undef __GNUC__\n#define __GNUC__ 7\nint x=__GNUC__; int main(){}\n",o);
      r = take(computeRetention(m)); check(r.disabledReasons.empty(),"source macro override ignored");
    });
    run("predefined rescan", [&] {
      m = extract("#define CAT_I(a,b) a##b\n#define CAT(a,b) CAT_I(a,b)\nint x=CAT(__GNUC,__); int main(){}\n",o);
      r = take(computeRetention(m)); check(!r.disabledReasons.empty(),"synthesized predefined name accepted");
      check(std::any_of(m.expansions.begin(),m.expansions.end(),[](const auto& x) { return x.name == "__GNUC__" && x.predefined; }),"predefined rescan fact missing: __GNUC__ from CAT(__GNUC,__); recorded:" + macroFacts());
      check(std::any_of(m.expansions.begin(),m.expansions.end(),[](const auto& x) {
        return x.name == "__GNUC__" && x.parse.state == MacroState::known && x.parse.definition && x.parse.definition->replacement == "4";
      }),"rescanned __GNUC__ parse value missing; recorded:" + macroFacts());
      check(std::any_of(m.expansions.begin(),m.expansions.end(),[](const auto& x) { return x.paste && x.externalPasteOperand; }),
            "CAT(__GNUC,__) external paste fact missing; recorded:" + macroFacts());
    });
    run("direct predefined", [&] {
      m = extract("int x=__GNUC__; int main(){}\n",o); r = take(computeRetention(m));
      check(!r.disabledReasons.empty(),"unknown predefined value accepted");
      check(std::any_of(m.expansions.begin(),m.expansions.end(),[](const auto& x) {
        return x.name == "__GNUC__" && x.predefined && x.parse.definition && x.parse.definition->replacement == "4";
      }),"direct __GNUC__ compiler fact missing; recorded:" + macroFacts());
    });
    run("source predefined paste override", [&] {
      m = extract("#undef __GNUC__\n#define __GNUC__ 7\n#define CAT(a,b) a##b\nint x=CAT(__GNUC,__); int main(){}\n",o);
      r = take(computeRetention(m)); check(r.disabledReasons.empty(),"source rescan override ignored");
      check(std::any_of(m.expansions.begin(),m.expansions.end(),[](const auto& x) {
        return x.name == "__GNUC__" && !x.predefined && x.parse.definition && x.parse.definition->replacement == "7";
      }),"source __GNUC__ rescan fact missing; recorded:" + macroFacts());
    });
    // D3d-4: GCC spells __SCHAR_MAX__ as 0x7f; Clang as 127.
    auto gccSchar = [&] {
      auto fact = [](std::string v) { return MacroFact{MacroState::known,Macro{false,{},false,std::move(v)},{FactOrigin::compilerBuiltin}}; };
      for (auto [name,value] : {std::pair{"__CHAR_BIT__","8"},{"__SIZEOF_INT__","4"},{"__SIZEOF_LONG__","8"},{"__SIZEOF_LONG_LONG__","8"}})
        m.judgeInitial[name] = fact(value);
      bool found = false;
      for (auto& x : m.expansions) if (x.name == "__SCHAR_MAX__") { x.judge = fact("0x7f"); found = true; }
      check(found,"__SCHAR_MAX__ expansion missing; recorded:" + macroFacts());
      r = take(computeRetention(m));
    };
    run("equal integer literal predefined", [&] {
      m = extract("int x=__SCHAR_MAX__; int main(){}\n",o);
      check(m.parseWidths == LiteralWidths{32,64,64},"parse widths missing");
      check(std::none_of(m.expansions.begin(),m.expansions.end(),[](const auto& x) { return x.hashOperator; }),"spurious hash operator");
      gccSchar(); check(r.disabledReasons.empty(),"equal hex/decimal literal rejected");
    });
    run("stringized equal literal predefined", [&] {
      m = extract("#define STR(x) #x\n#define XSTR(x) STR(x)\nconst char* s=XSTR(__SCHAR_MAX__); int main(){}\n",o);
      check(std::any_of(m.expansions.begin(),m.expansions.end(),[](const auto& x) { return x.name == "__SCHAR_MAX__" && x.hashOperator; }),
            "stringized __SCHAR_MAX__ not flagged; recorded:" + macroFacts());
      gccSchar(); check(!r.disabledReasons.empty(),"stringized spelling accepted");
    });
    run("pasted equal literal predefined", [&] {
      m = extract("#define CAT(a,b) a##b\n#define XCAT(a,b) CAT(a,b)\nint x=XCAT(__SCHAR_MAX__,0); int main(){}\n",o);
      check(std::any_of(m.expansions.begin(),m.expansions.end(),[](const auto& x) { return x.name == "__SCHAR_MAX__" && x.hashOperator; }),
            "pasted __SCHAR_MAX__ not flagged; recorded:" + macroFacts());
      gccSchar(); check(!r.disabledReasons.empty(),"pasted spelling accepted");
    });
    run("dependent qualified lookup", [&] {
      m = extract("template<class T> void run(){T::opaque();} int main(){}\n",o);
      r = take(computeRetention(m)); check(r.disabledReasons.empty(),"dependent qualified lookup disabled");
    });
    run("leading attributes", [&] {
      m = extract("[[nodiscard]] int unused(){return 1;}\n[[maybe_unused]] int a,b;\nalignas(16) int aligned;\n[[gnu::always_inline]] inline int inlined(){return 0;}\nint main(){}\n",o);
      r = take(computeRetention(m)); check(r.disabledReasons.empty(),"ordinary attributes disabled");
      check(!family(m,"unused").ranges.empty() && m.text.substr(family(m,"unused").ranges[0].begin,13) == "[[nodiscard]]","leading standard attribute not owned");
      check(!family(m,"a").ranges.empty() && m.text.substr(family(m,"a").ranges[0].begin,16) == "[[maybe_unused]]","shared leading attribute not owned");
      check(!family(m,"aligned").ranges.empty() && m.text.substr(family(m,"aligned").ranges[0].begin,7) == "alignas","leading alignment not owned");
      check(!family(m,"inlined").ranges.empty() && m.text.substr(family(m,"inlined").ranges[0].begin,5) == "[[gnu","scoped attribute not owned");
    });
    run("attribute macro", [&] {
      m = extract("#define INLINE __attribute__((always_inline))\nINLINE inline int f(){return 0;} int main(){}\n",o);
      r = take(computeRetention(m)); check(r.disabledReasons.empty(),"attribute macro disabled");
      check(!family(m,"f").pinned.empty(),"attribute macro not pinned");
    });
    auto attributeCase = [&](std::string text, const std::set<std::string>& live,
                             const std::set<std::string>& dead) {
      m = extract(std::move(text),o); r = take(computeRetention(m));
      check(r.disabledReasons.empty(),"no_unique_address extraction disabled");
      for (const auto& label : live) check(kept(label),"dependency removed: " + label);
      for (const auto& label : dead) check(!kept(label),"unrelated declaration kept: " + label);
      check(r.removedBytes > 0,"no actual removal");
      const auto out = take(renderSource(m,&r));
      take(renderingInvariant(m,&r,out.provenance.text));
      take(staticNameInvariant(m,r,out.provenance.text));
      return out.provenance.text;
    };
    run("no_unique_address keeps complete record layout", [&] {
      const std::string record = "struct Live { [[no_unique_address]] Empty empty; [[no_unique_address]] Value value; int tail; };";
      const auto out = attributeCase("struct Empty {}; struct Value { int n; };\n" + record +
          "\nint unused(){return 9;} int main(){return sizeof(Live)==0;}\n",
          {"Empty","Value","Live","main"},{"unused"});
      check(out.find(record) != out.npos,"record members or attribute spelling changed");
      check(out.find("unused") == out.npos,"unused function text retained");
    });
    run("no_unique_address unused whole record", [&] {
      const auto out = attributeCase(
          "struct Empty {}; struct Dead { [[no_unique_address]] Empty empty; int tail; };\n"
          "int live(){return 0;} int main(){return live();}\n",
          {"live","main"},{"Empty","Dead"});
      check(out.find("Dead") == out.npos && out.find("no_unique_address") == out.npos,"unused attributed record text retained");
      check(out.find("int live(){return 0;}") != out.npos,"live function text changed");
    });
    run("no_unique_address templates and nested records", [&] {
      const std::string record = "template<class T> struct Box { struct Nested { [[no_unique_address]] T empty; int n=3; }; [[no_unique_address]] T other; Nested nested; };";
      const auto out = attributeCase("struct Empty {};\n" + record +
          "\ntemplate<class T> struct Dead { [[no_unique_address]] T empty; };\n"
          "int unused(){return 9;} int main(){Box<Empty> box; return box.nested.n-3;}\n",
          {"Empty","Box","main"},{"Dead","unused"});
      check(out.find(record) != out.npos,"nested/template members changed");
      check(out.find("Dead") == out.npos && out.find("unused") == out.npos,"dead template/function text retained");
    });
    run("no_unique_address default member initializer dependency", [&] {
      const auto out = attributeCase(
          "int initial(){return 7;} int unused(){return 9;}\n"
          "struct Live { [[no_unique_address]] int value=initial(); };\n"
          "int main(){Live x; return x.value-7;}\n",
          {"initial","Live","main"},{"unused"});
      check(out.find("int initial(){return 7;}") != out.npos && out.find("value=initial()") != out.npos,"initializer dependency/spelling lost");
      check(out.find("unused") == out.npos,"unused function text retained");
    });
    run("no_unique_address constructor and destructor dependencies", [&] {
      const auto out = attributeCase(
          "void construct(){} void destroy(){} int unused(){return 9;}\n"
          "struct Member { Member(); ~Member(); };\n"
          "Member::Member(){construct();} Member::~Member(){destroy();}\n"
          "struct Live { [[no_unique_address]] Member member; };\n"
          "int main(){Live x;}\n",
          {"construct","destroy","Member","Member::Member","Member::~Member","Live","main"},{"unused"});
      check(out.find("Member::Member(){construct();}") != out.npos && out.find("Member::~Member(){destroy();}") != out.npos,"special member definitions lost");
      check(out.find("[[no_unique_address]] Member member;") != out.npos,"attributed field lost");
      check(out.find("unused") == out.npos,"unused function text retained");
    });
    run("no_unique_address macro expansion", [&] {
      const auto out = attributeCase(
          "#define ATTR(name) [[name]]\n#define NUA ATTR(no_unique_address)\n"
          "struct Empty {}; struct Live { NUA Empty empty; int n; };\n"
          "struct Dead { NUA Empty empty; };\n"
          "int unused(){return 9;} int main(){return sizeof(Live)==0;}\n",
          {"Empty","Live","main"},{"Dead","unused"});
      check(out.find("struct Live { NUA Empty empty; int n; };") != out.npos,"macro-produced field spelling lost");
      check(out.find("Dead") == out.npos && out.find("unused") == out.npos,"macro field pinned unused record/function");
    });
    run("no_unique_address attribute list", [&] {
      const auto out = attributeCase(
          "struct Live { [[maybe_unused, no_unique_address]] int value; };\n"
          "int unused(){return 9;} int main(){return sizeof(Live)==0;}\n",
          {"Live","main"},{"unused"});
      check(out.find("[[maybe_unused, no_unique_address]] int value;") != out.npos,"attribute list changed");
      check(out.find("unused(){") == out.npos,"unused function text retained");
    });
    for (const auto& [name,text] : std::vector<std::pair<const char*,const char*>>{
        {"static member","struct Bad { [[no_unique_address]] static int value; };"},
        {"bit-field","struct Bad { [[no_unique_address]] unsigned value:1; };"},
        {"arguments","struct Bad { [[no_unique_address(1)]] int value; };"},
        {"empty argument list","struct Bad { [[no_unique_address()]] int value; };"},
        {"msvc spelling","struct Bad { [[msvc::no_unique_address]] int value; };"},
        {"unknown scope","struct Bad { [[other::no_unique_address]] int value; };"},
        {"using scope","struct Bad { [[using msvc: no_unique_address]] int value; };"},
        {"unaudited underscored spelling","struct Bad { [[__no_unique_address__]] int value; };"},
        {"GNU spelling","struct Bad { __attribute__((no_unique_address)) int value; };"},
        {"non-member","[[no_unique_address]] int value;"},
        {"statement","int f(){ [[no_unique_address]] return 0; }"},
        {"macro vendor spelling","#define NUA [[msvc::no_unique_address]]\nstruct Bad { NUA int value; };"},
        {"macro arguments","#define NUA(x) [[no_unique_address(x)]]\nstruct Bad { NUA(1) int value; };"}}) {
      run((std::string("no_unique_address rejects ") + name).c_str(), [&] {
        // A successful control proves this worker can remove declarations;
        // the unsupported variant must instead preserve the entire input.
        const auto control = attributeCase(
            "struct Live { [[no_unique_address]] int value; };\n"
            "int unused(){return 9;} int main(){return sizeof(Live)==0;}\n",
            {"Live","main"},{"unused"});
        check(control.find("unused") == control.npos && control.find("[[no_unique_address]] int value;") != control.npos,"control removal/preservation failed");
        m = extract(std::string(text) + "\nint unused(){return 9;} int main(){}\n",o);
        r = take(computeRetention(m));
        check(std::any_of(r.disabledReasons.begin(),r.disabledReasons.end(),[](const auto& why) {
          return why.starts_with("unsupported attribute:");
        }),"missing unsupported attribute refusal");
        check(r.removedBytes == 0,"unsupported attribute permitted removal");
        for (const auto& f : r.families) check(f.kept,"disabled extraction removed a family");
        check(!renderSource(m,&r).ok(),"disabled extraction rendered a pruning candidate");
        const auto out = take(renderSource(m));
        take(renderingInvariant(m,nullptr,out.provenance.text));
        check(out.provenance.text.find(text) != out.provenance.text.npos &&
              out.provenance.text.find("int unused(){return 9;}") != out.provenance.text.npos,"fallback lost source text");
      });
    }
    run("attribute roots", [&] {
      m = extract("__attribute__((constructor(200))) void startup(){}\n"
                  "__attribute__((destructor(200))) void shutdown(){}\n"
                  "__attribute__((used)) int retained=42;\n"
                  "__attribute__((used)) void retainedFunction(){}\n"
                  "__attribute__((section(\"my_section\"))) int sectioned=1;\n"
                  "__attribute__((weak)) int weakValue=1;\n"
                  "__attribute__((visibility(\"hidden\"))) void visible(){}\n"
                  "void unused(){} int main(){}\n",o); r = take(computeRetention(m));
      check(r.disabledReasons.empty(),"root attributes disabled");
      for (const auto* name : {"startup","shutdown","retained","retainedFunction","sectioned","weakValue","visible"}) {
        check(kept(name),std::string("attribute root removed: ")+name);
        check(!family(m,name).root.empty(),std::string("attribute not rooted: ")+name);
      }
      check(!kept("unused"),"unrelated function retained");
    });
    run("alias and asm symbols", [&] {
      m = extract("void target() asm(\"target_symbol\"); void target(){}\n"
                  "void alias() __attribute__((alias(\"target_symbol\")));\n"
                  "void mangled(){} void another() __attribute__((alias(\"_Z7mangledv\")));\n"
                  "int data asm(\"data_symbol\")=1;\n"
                  "asm(\".quad data_symbol\");\n"
                  "void caller(){asm(\"call target_symbol\");}\n"
                  "void unused(){} int main(){}\n",o); r = take(computeRetention(m));
      check(r.disabledReasons.empty(),"resolved symbols disabled");
      for (const auto* name : {"target","alias","mangled","another","data","caller"}) check(kept(name),std::string("symbol root removed: ")+name);
      check(!kept("unused"),"unrelated function retained");
    });
    run("unresolved alias", [&] {
      m = extract("void alias() __attribute__((alias(\"missing_symbol\"))); int main(){}\n",o);
      r = take(computeRetention(m)); check(!r.disabledReasons.empty(),"unresolved alias accepted");
    });
    run("unresolved asm", [&] {
      m = extract("int main(){asm(\"call missing_symbol\");}\n",o);
      r = take(computeRetention(m)); check(!r.disabledReasons.empty(),"unresolved asm accepted");
    });
    run("empty asm operands", [&] {
      m = extract("int value; void unused(){asm(\"\" : : \"r\"(&value));} int main(){}\n",o);
      r = take(computeRetention(m)); check(r.disabledReasons.empty(),"empty asm disabled");
      check(kept("value") && kept("unused"),"asm operand closure removed");
    });
    run("opaque asm", [&] {
      m = extract("int main(){asm(\"opaque instruction\");}\n",o); r = take(computeRetention(m));
      check(!r.disabledReasons.empty(),"opaque asm accepted");
    });
    run("stripped keep annotation", [&] {
      Configuration c; c.probe.triple = "x86_64-unknown-linux-gnu"; c.probe.dialect = {"c++20",false,202002};
      ResolveResult flat; flat.text = "// distill: keep\nint preserved(){return 1;} int main(){}\n";
      auto input = take(preparePruning(c,{},flat));
      const auto newline = input.text.find('\n'); input.text.replace(0,newline,newline,' ');
      m = extracted(input,o); r = take(computeRetention(m));
      check(r.disabledReasons.empty() && !m.annotations.empty(),"keep metadata lost after stripping comment");
      check(family(m,"preserved").names.contains("preserved"),"kept declaration missing");
    });
    run("parse recovery", [&] {
      m = extract("int broken(; int main(){}\n",o); r = take(computeRetention(m));
      check(!r.disabledReasons.empty() && m.families.empty(),"recovery AST analyzed");
    });
    run("out-of-line non-type parameters", [&] {
      m = extract("template<int N> struct Box { int get(); static int value; };\ntemplate<int N> int Box<N>::get(){return N;}\ntemplate<int N> int Box<N>::value=N;\nint unused(){return 0;} int main(){return Box<4>{}.get()+Box<4>::value;}\n",o); r = take(computeRetention(m));
      check(r.disabledReasons.empty(),"ownership unexpectedly disabled");
      check(kept("Box"),"referenced family removed");
      check(!kept("unused"),"unused function retained");
    });
    run("parameter-like lexical ownership", [&] {
      m = extract("template<class T> concept Add = requires(T a, T b){ a+b; };\n"
                  "template<class T> concept Function = requires(T (*f)(T named)){ f(T{}); };\n"
                  "template<class T> concept Lambda = requires { [](T parameter){ return parameter; }; };\n"
                  "template<template<class Inner> class C, class T> struct Wrap { C<T> value; };\n"
                  "template<class T> struct Item { T value; };\n"
                  "auto global=[]<class T>(T argument){return argument;};\n"
                  "template<class T> int run(T input){\n"
                  "  auto f=[captured=input](T parameter){return captured+parameter;};\n"
                  "  T array[2]={input,input}; auto [left,right]=array;\n"
                  "  for (auto element:array) left+=element; return f(left)+right; }\n"
                  "int unused(){return 0;}\n"
                  "int main(){static_assert(Add<int> && Function<int> && Lambda<int>);\n"
                  "  Wrap<Item,int> x{}; return global(run(x.value.value));}\n",o);
      r = take(computeRetention(m)); check(r.disabledReasons.empty(),"local ownership disabled");
      for (const auto* name : {"Add","Function","Lambda","Wrap","global","run"}) check(kept(name),std::string("enclosing family removed: ")+name);
      check(!kept("unused"),"unrelated function retained");
    });
    run("nested template parameters", [&] {
      m = extract("template<template<class> class C, class T> struct Wrap { C<T> value; };\ntemplate<class T> struct Item { T x; };\nint unused(){return 0;} int main(){Wrap<Item,int> x{};return x.value.x;}\n",o); r = take(computeRetention(m));
      check(r.disabledReasons.empty(),"ownership unexpectedly disabled");
      check(kept("Wrap"),"referenced family removed");
      check(!kept("unused"),"unused function retained");
    });
    run("partial specialization members", [&] {
      m = extract("template<class T> struct Box;\ntemplate<class T> struct Box<T*> { struct Inner { T x; }; static int get(){Inner v{};return v.x;} };\nint unused(){return 0;} int main(){return Box<int*>::get();}\n",o); r = take(computeRetention(m));
      check(r.disabledReasons.empty(),"ownership unexpectedly disabled");
      check(kept("Box"),"referenced family removed");
      check(!kept("unused"),"unused function retained");
    });
    run("variable template instantiations", [&] {
      m = extract("template<class T> constexpr int value=sizeof(T);\nint unused(){return 0;} int main(){return value<int>;}\n",o); r = take(computeRetention(m));
      check(r.disabledReasons.empty(),"ownership unexpectedly disabled");
      check(kept("value"),"referenced family removed");
      check(!kept("unused"),"unused function retained");
    });
    run("deduction guides", [&] {
      m = extract("template<class T> struct Box { T value; Box(T x):value(x){} };\nBox(int)->Box<long>;\nint unused(){return 0;} int main(){Box x(1);return x.value;}\n",o); r = take(computeRetention(m));
      check(r.disabledReasons.empty(),"ownership unexpectedly disabled");
      check(kept("Box"),"referenced family removed");
      check(!kept("unused"),"unused function retained");
    });
    run("implicit deduction guides", [&] {
      m = extract("template<class T> struct Box { T value; Box(T x):value(x){} };\nint unused(){return 0;} int main(){Box x(1);return x.value;}\n",o); r = take(computeRetention(m));
      check(r.disabledReasons.empty(),"ownership unexpectedly disabled");
      check(kept("Box"),"referenced family removed");
      check(!kept("unused"),"unused function retained");
    });
    run("namespace lambda closure", [&] {
      m = extract("auto fn=[]<class T>(T x){return x;};\nint unused(){return 0;} int main(){return fn(1);}\n",o); r = take(computeRetention(m));
      check(r.disabledReasons.empty(),"ownership unexpectedly disabled");
      check(kept("fn"),"referenced family removed");
      check(!kept("unused"),"unused function retained");
    });
    run("local lambda closure", [&] {
      m = extract("template<class T> int run(T x){auto f=[x]{return x;};return f();}\nint unused(){return 0;} int main(){return run(1);}\n",o); r = take(computeRetention(m));
      check(r.disabledReasons.empty(),"ownership unexpectedly disabled");
      check(kept("run"),"referenced family removed");
      check(!kept("unused"),"unused function retained");
    });
    run("using shadows", [&] {
      m = extract("namespace n { int value(){return 1;} }\nusing n::value;\nstruct Base { int get(){return value();} }; template<class T> struct Box:T { using T::get; };\nint unused(){return 0;} int main(){return Box<Base>{}.get();}\n",o); r = take(computeRetention(m));
      check(r.disabledReasons.empty(),"ownership unexpectedly disabled");
      check(kept("Box"),"referenced family removed");
      check(!kept("unused"),"unused function retained");
    });
    run("builtin template arguments", [&] {
      m = extract("struct Payload {};\nusing Chosen=__type_pack_element<0,Payload,int>;\nint unused(){return 0;} int main(){Chosen x;(void)x;}\n",o); r = take(computeRetention(m));
      check(r.disabledReasons.empty(),"ownership unexpectedly disabled");
      check(kept("Payload"),"referenced family removed");
      check(!kept("unused"),"unused function retained");
    });
    run("builtin functions and C++ defaults", [&] {
      m = extract("#if !defined(__EXCEPTIONS) || !defined(__GXX_RTTI) || !defined(__DEPRECATED) || defined(__NO_MATH_ERRNO__)\n"
                  "#error missing C++ driver defaults\n#endif\n"
                  "static_assert(__builtin_popcount(7) == 3);\n"
                  "static_assert(__builtin_clzll(1ULL) == 63);\n"
                  "struct Base { virtual ~Base() = default; }; struct Derived : Base {};\n"
                  "int main(){Derived d; Base* b=&d; if (!dynamic_cast<Derived*>(b)) __builtin_unreachable();\n"
                  "try { throw __builtin_popcount(7); } catch(int n) {\n"
                  "return __builtin_expect_with_probability(n + __builtin_ctz(8), 6, 0.9); }}\n",o);
      r = take(computeRetention(m));
      check(m.disabledReasons.empty(),"builtin/defaults parse unexpectedly disabled");
      check(r.disabledReasons.empty(),"builtin/defaults retention unexpectedly disabled");
      check(kept("main"),"main missing after builtin parsing");
    });
    run("scalar overflow builtin argument dependencies", [&] {
      m = extract("int value(){return 2;}\nint storage;\nint* destination(){return &storage;}\n"
                  "int unrelated(){return 99;}\n"
                  "int main(){bool a=__builtin_add_overflow(value(),3,destination());\n"
                  "bool b=__builtin_sub_overflow(value(),1,destination());\n"
                  "bool c=__builtin_mul_overflow(value(),4,destination());return a||b||c;}\n",o);
      r = take(computeRetention(m));
      check(m.disabledReasons.empty(),"overflow extraction disabled");
      check(r.disabledReasons.empty(),"overflow retention disabled");
      check(kept("value"),"value argument dependency lost");
      check(kept("destination"),"pointer argument dependency lost");
      check(kept("storage"),"pointer destination dependency lost");
      check(!kept("unrelated"),"unrelated function retained");
    });
    // --no-line-markers observations (docs/reviews/2026-11-18-no-line-markers.md).
    auto hasUse = [&](std::string_view what) {
      return std::any_of(m.positionUses.begin(),m.positionUses.end(),[&](const auto& u) { return u.what.starts_with(what); });
    };
    auto uses = [&] {
      std::string s; for (const auto& u : m.positionUses) s += "\n    " + u.what + "@" + std::to_string(u.offset);
      return s;
    };
    run("line marker position uses in user code", [&] {
      m = extract("int f(){return 1;}\nint main(){return f();}\n",o);
      check(m.positionUses.empty(),"spurious position use:" + uses());
      m = extract("int main(){return __LINE__;}\n",o);
      check(hasUse("__LINE__ expanded"),"__LINE__ missing:" + uses());
      check(!m.positionUses.empty() && m.positionUses.front().offset == m.text.find("__LINE__"),"__LINE__ offset:" + uses());
      m = extract("int main(){return __builtin_COLUMN();}\n",o);
      check(hasUse("__builtin_COLUMN") || hasUse("source position builtin"),"__builtin_COLUMN missing:" + uses());
      m = extract("#define WHERE __FILE__\nconst char* f(){return WHERE;}\nint main(){}\n",o);
      check(hasUse("__FILE__ expanded"),"nested __FILE__ missing:" + uses());
      m = extract("#if __LINE__ > 0\nint x;\n#endif\nint main(){}\n",o);
      check(hasUse("__LINE__ expanded"),"condition __LINE__ missing:" + uses());
      m = extract("int where(int line = __builtin_LINE()){return line;}\nint main(){return where();}\n",o);
      check(hasUse("__builtin_LINE"),"user default __builtin_LINE missing:" + uses());
      m = extract("#define AT __builtin_FILE()\nconst char* f(){return AT;}\nint main(){}\n",o);
      check(hasUse("__builtin_FILE"),"macro-produced __builtin_FILE missing:" + uses());
      m = extract("const char* f(){return __PRETTY_FUNCTION__;}\nint main(){}\n",o);
      check(hasUse("__PRETTY_FUNCTION__"),"__PRETTY_FUNCTION__ missing:" + uses());
      // Third review: every function-name expression counts (fail closed);
      // a name can embed an unnamed type's presumed location.
      m = extract("const char* f(){return __func__;}\nint main(){}\n",o);
      check(hasUse("function name expression"),"__func__ missing:" + uses());
      m = extract("const char* g(){return __builtin_FUNCTION();}\nint main(){}\n",o);
      check(hasUse("source position builtin"),"__builtin_FUNCTION missing:" + uses());
      m = extract("enum { anon = 1 };\nstruct C { operator decltype(anon)() const { (void)__func__; return anon; } };\n"
                  "int main(){return C{} - 1;}\n",o);
      check(hasUse("function name expression"),"__func__ in a conversion to an unnamed enum missing:" + uses());
      m = extract("const char* seen;\n"
                  "int f(decltype([] {\n"
                  "  enum { e };\n"
                  "  struct C {\n"
                  "    operator decltype(e)() const {\n"
                  "      seen = __func__;\n"
                  "      return e;\n"
                  "    }\n"
                  "  };\n"
                  "  return int(C{});\n"
                  "}) cb) { return cb(); }\n"
                  "int main(){return f({});}\n",o);
      check(hasUse("function name expression"),"__func__ in a parameter type's conversion to an unnamed enum missing:" + uses());
      m = extract("#define S(x) #x\nconst char* s = S(__LINE__);\nint main(){}\n",o);
      check(m.positionUses.empty(),"a stringized __LINE__ is not expanded:" + uses());
    });
    run("line marker position uses from system headers", [&] {
      // Each case writes its own test-only -isystem header: any function-name
      // or position construct anywhere in the TU keeps the markers, so cases
      // must not share one.
      const auto dir = temp.path / "position-system";
      std::filesystem::create_directories(dir);
      std::size_t header = 0;
      auto withHeader = [&](const std::string& text, const std::string& body) {
        const auto name = "case" + std::to_string(header++) + ".h";
        { std::ofstream h(dir / name, std::ios::binary); h << "#pragma once\n" << text; }
        Configuration c; c.probe.triple = "x86_64-unknown-linux-gnu"; c.probe.dialect = {"c++20",false,202002};
        SearchDirectory d; d.path = dir; d.kind = IncludeKind::system; c.probe.searchLists.directories.push_back(d);
        ResolveResult flat; flat.text = "#include <" + name + ">\n" + body;
        for (std::size_t i = 0, line = 1; i < flat.text.size(); ++i) if (i == 0 || flat.text[i-1] == '\n') flat.lineMap.push_back(SourceLocation{"original.hpp",line++});
        m = extracted(take(preparePruning(c,{},flat)),o);
        check(std::none_of(m.disabledReasons.begin(),m.disabledReasons.end(),[](const auto& why) { return why.starts_with("Clang parse error"); }),"header case does not parse");
      };
      const std::string where = "inline int where(int line = __builtin_LINE()) { return line; }\n";
      const std::string signature = "template<class T> const char* signature(T) { return __PRETTY_FUNCTION__; }\n";
      // A parameter's default is evaluated only at a call that uses it.
      withHeader(where + "template<class T> int plain(T) { return 0; }\n","int main(){return plain(1) + where(7) - 7;}\n");
      check(m.positionUses.empty(),"plain program blocked (unused default argument counted):" + uses());
      withHeader("inline int column(int c = __builtin_COLUMN()) { return c; }\n","int main(){return column();}\n");
      check(hasUse("source position builtin"),"system column default missing:" + uses());
      withHeader("namespace std { struct source_location { unsigned column() const { return __builtin_COLUMN(); } }; }\n",
                 "int main(){return std::source_location{}.column();}\n");
      check(hasUse("value of type") && hasUse("source position builtin"),"source_location::column missing:" + uses());
      withHeader(where,"int main(){return where();}\n");
      check(hasUse("source position builtin (outside user code"),"system default argument at a call missing:" + uses());
      check(!hasUse("__builtin_LINE"),"system token attributed to user code:" + uses());
      withHeader("struct Logged { int line = __builtin_LINE(); };\n","int main(){Logged l{}; return l.line;}\n");
      check(hasUse("source position builtin"),"default member initializer missing:" + uses());
      withHeader("namespace std { inline namespace __v { struct stacktrace_entry { int source_line() const { return 0; } };\n"
                 "inline stacktrace_entry here() { return {}; } } }\n","int main(){return std::here().source_line();}\n");
      check(hasUse("value of type"),"stacktrace-typed value missing:" + uses());
      withHeader("namespace std::meta { inline int probe() { return 0; } }\n","int main(){return std::meta::probe();}\n");
      check(hasUse("use of std::meta::probe"),"std::meta use missing:" + uses());
      withHeader("#define NONSTANDARD_CONSTANT 3\n","int main(){return NONSTANDARD_CONSTANT - 3;}\n");
      check(hasUse("non-standard system macro NONSTANDARD_CONSTANT expanded"),"non-standard system macro missing:" + uses());
      withHeader("#define EOF (-1)\n","int main(){return EOF + 1;}\n");
      check(m.positionUses.empty(),"standard macro EOF blocked:" + uses());
      withHeader("#define assert(e) ((e) ? (void)0 : (void)__LINE__)\n","int main(){assert(1); return 0;}\n");
      check(hasUse("__LINE__ expanded") && !hasUse("non-standard system macro"),"assert without NDEBUG:" + uses());
      // Codex reproducers: a name in any system template, whatever its
      // arguments (closure, int, nested Outer<closure>::Inner, unnamed-enum
      // integral value, 20-deep nesting), local classes at any depth, dump_struct.
      withHeader(signature,"int main(){return signature([]{})[0] == 0;}\n");
      check(hasUse("function name expression"),"system __PRETTY_FUNCTION__ with a closure:" + uses());
      withHeader(signature,"int main(){return signature(1)[0] == 0;}\n");
      check(hasUse("function name expression"),"instantiation with int arguments:" + uses());
      withHeader(signature + "template<class T> struct Outer { struct Inner {}; };\n",
                 "int main(){auto l = []{}; return signature(Outer<decltype(l)>::Inner{})[0] == 0;}\n");
      check(hasUse("function name expression"),"nested type of a closure specialization:" + uses());
      // Use an in-range value without a named enumerator so the printed
      // argument includes the unnamed enum's type and source location.
      withHeader("template<auto V> const char* valueSignature() { return __PRETTY_FUNCTION__; }\n",
                 "enum { anon_value = 1 };\nint main(){return valueSignature<static_cast<decltype(anon_value)>(0)>()[0] == 0;}\n");
      check(hasUse("function name expression"),"integral argument of an unnamed enum:" + uses());
      std::string nested = "decltype(l)";
      for (int i = 0; i < 20; ++i) nested = "W<" + nested + ">";
      withHeader(signature + "template<class T> struct W {};\n","int main(){auto l = []{}; return signature(" + nested + "{})[0] == 0;}\n");
      check(hasUse("function name expression"),"deeply nested argument:" + uses());
      withHeader("template<class T> struct Box { const char* name() const { return __func__; } };\n",
                 "struct Mine {};\nint main(){return Box<Mine>{}.name()[0] == 0;}\n");
      check(hasUse("function name expression"),"system __func__ in a specialization member:" + uses());
      withHeader("template<class T> const char* make(T) {\n"
                 "  struct Local { struct Nested { const char* name() { return __PRETTY_FUNCTION__; } }; };\n"
                 "  return typename Local::Nested{}.name(); }\n","int main(){return make([]{})[0] == 0;}\n");
      check(hasUse("function name expression"),"nested local class in a system template:" + uses());
      withHeader("extern \"C\" int printf(const char*, ...);\n"
                 "template<class T> int dumped(T v) { __builtin_dump_struct(&v, printf); return 0; }\n",
                 "int main(){struct { int a; } s{}; return dumped(s);}\n");
      check(hasUse("builtin __builtin_dump_struct"),"__builtin_dump_struct in a system template:" + uses());
    });
    run("standard library and builtin functions", [&] {
      // Use the native GCC's target and header search, just like extraction
      // cases; the header-free synthetic models above use a fixed target.
      auto command = take(parseCompileCommand({"g++","-std=gnu++20","empty.cpp"},temp.path));
      { std::ofstream empty(temp.path / "empty.cpp"); }
      ProbeOptions po; po.temporaryRoot = temp.path;
      const auto configuration = take(probeConfiguration(command,"builtin regression",po));
      ResolveResult flat; flat.text = "#include <bits/stdc++.h>\nint main(){return __builtin_popcount(7);}\n";
      flat.lineMap = {SourceLocation{"original.hpp",1},SourceLocation{"original.hpp",2}};
      m = extracted(take(preparePruning(configuration,{},flat,&configuration.facts)),o);
      check(m.disabledReasons.empty(),"bits/stdc++.h with __builtin_popcount failed to parse");
      check(!family(m,"main").ranges.empty(),"main missing after standard-library parsing");
    });
    run("macro generated declarations", [&] {
      m = extract("#define DEFINE(name) template<class T> struct name { T get(){return T{};} };\nDEFINE(Box)\nint unused(){return 0;} int main(){return Box<int>{}.get();}\n",o); r = take(computeRetention(m));
      check(r.disabledReasons.empty(),"ownership unexpectedly disabled");
      check(kept("Box"),"referenced family removed");
      check(!kept("unused"),"unused function retained");
    });
    std::cout << checks << " extraction model cases passed; " << assertions << " checks, " << failed << " failures\n"; return failed ? 1 : 0;
  } catch (const Error& e) { std::cerr << e.message << '\n'; return 1; }
  catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
