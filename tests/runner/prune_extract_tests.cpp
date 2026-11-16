// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/prune/extract.h"
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
    std::string active;
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
      active = name; m = {}; r = {}; const auto before = failed;
      try { test(); }
      catch (const Error& e) { check(false,e.message); }
      catch (const std::exception& e) { check(false,e.what()); }
      for (const auto& why : m.disabledReasons)
        if (why.starts_with("pruning worker") || why.starts_with("invalid pruning worker result")) check(false,why);
      if (before == failed) { ++checks; std::cout << "PASS " << active << '\n'; }
      else {
        for (const auto& why : m.disabledReasons) std::cerr << "  disabled: " << why << '\n';
        for (const auto& why : r.disabledReasons)
          if (std::find(m.disabledReasons.begin(),m.disabledReasons.end(),why) == m.disabledReasons.end())
            std::cerr << "  disabled: " << why << '\n';
        for (FamilyId id = 0; id < m.families.size(); ++id)
          std::cerr << "  " << m.families[id].label << ": "
                    << (id < r.families.size() ? r.families[id].reason : "retention unavailable") << '\n';
      }
    };
    run("template ownership", [&] {
      m = extract("template<class T> struct Box { void f(); };\ntemplate<class T> void Box<T>::f() { int local=0; (void)local; }\nint unused() { return 0; }\nint main() { Box<int> x; x.f(); }\n",o);
      r = take(computeRetention(m)); check(r.disabledReasons.empty(),"ownership unexpectedly disabled");
      const auto& box = family(m,"Box"); check(!box.ranges.empty(),"missing template range");
      check(!box.ranges.empty() && m.text.substr(box.ranges[0].begin,8) == "template","template prefix missing");
      check(!box.ranges.empty() && box.ranges[0].end > 0 && m.text[box.ranges[0].end-1] == ';',"record terminator missing");
      check(!box.names.contains("local") && !box.names.contains("f"),"member/local indexed");
      check(!m.unions.empty(),"out-of-line member not aggregated");
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
    run("opaque attribute", [&] {
      m = extract("int x __attribute__((used)); int main(){}\n",o); r = take(computeRetention(m));
      check(!r.disabledReasons.empty(),"opaque attribute accepted");
    });
    run("opaque asm", [&] {
      m = extract("int main(){asm(\"\");}\n",o); r = take(computeRetention(m));
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
