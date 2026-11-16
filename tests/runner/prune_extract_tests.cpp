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
PruningModel extract(std::string text, const PruneWorkerOptions& o) {
  Configuration c; c.probe.triple = "x86_64-unknown-linux-gnu"; c.probe.dialect = {"c++20",false,202002};
  ResolveResult flat; flat.text = std::move(text);
  for (std::size_t i = 0, line = 1; i < flat.text.size(); ++i) if (i == 0 || flat.text[i-1] == '\n') flat.lineMap.push_back(SourceLocation{"original.hpp",line++});
  auto m = take(preparePruning(c,{},flat));
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
      check(!box.ranges.empty() && box.ranges[0].end > 0 && m.text[box.ranges[0].end-1] == ';',"record terminator missing");
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
      const auto& f = family(m,"Dead::f");
      check(f.ranges.size() == 1 && std::string_view(m.text).substr(f.ranges[0].begin).starts_with("template<class T> template<class U>"),"outer header orphaned");
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
