// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/prune/extract.h"
#include "core/render/render.h"
#include "core/path.h"
#include "core/temporary.h"
#include <cstdlib>
#include <fstream>
#include <iostream>

namespace {
using namespace distill;
template<class T> T take(Result<T> r) { if (!r.ok()) throw Error{r.error().message}; return std::move(r.value()); }
void check(bool ok, const std::string& why) { if (!ok) throw Error{why}; }
std::string read(const std::filesystem::path& p) {
  std::ifstream f(p); check(bool(f),"cannot read " + pathToUtf8(p));
  return {std::istreambuf_iterator<char>(f),{}};
}
}
int main(int argc, char** argv) {
  if (argc != 3) return 1;
  try {
    TemporaryDirectory temp(".member-review-",std::filesystem::current_path());
    { std::ofstream f(temp.path / "empty.cpp"); }
    const std::string gcc = std::getenv("DISTILL_TEST_GXX") ? std::getenv("DISTILL_TEST_GXX") : "g++";
    auto command = take(parseCompileCommand({gcc,"-std=c++23","empty.cpp"},temp.path));
    ProbeOptions po; po.temporaryRoot = temp.path;
    auto config = take(probeConfiguration(command,"independent member review",po));
    PruneWorkerOptions worker; worker.executable = std::filesystem::absolute(argv[1]); worker.temporaryRoot = temp.path;
    unsigned passed = 0, failed = 0;
    auto run = [&](const std::string& name, auto test) {
      try { test(); ++passed; std::cout << "PASS " << name << '\n'; }
      catch (const Error& e) { ++failed; std::cerr << "FAIL " << name << ": " << e.message << '\n'; }
    };
    auto analyze = [&](const std::string& text, bool assume) {
      ResolveResult flat; flat.text = text;
      for (std::size_t i = 0, line = 1; i < text.size(); ++i)
        if (!i || text[i-1] == '\n') flat.lineMap.push_back(SourceLocation{"main.cpp",line++});
      auto m = take(preparePruning(config,{},flat)); m.assumeInstantiationEffects = assume;
      m = take(extractPruning(m,worker)); auto r = take(computeRetention(m));
      std::string reasons; for (const auto& why : r.disabledReasons) reasons += why + '\n';
      check(reasons.empty(),reasons);
      auto out = take(renderSource(m,&r)).provenance.text;
      take(renderingInvariant(m,&r,out)); take(staticNameInvariant(m,r,out));
      return std::tuple{m,r,out};
    };
    auto unit = [&](const PruningModel& m, const PruningResult& r, const std::string& name, bool kept) {
      bool found = false;
      for (FamilyId i = 0; i < m.families.size(); ++i) if (m.families[i].label == name) {
        found = true; check(r.families[i].kept == kept,name + ": " + r.families[i].reason);
      }
      check(found,"missing " + name);
    };
    auto verify = [&](std::string text, int exitCode, bool judge = false) {
      // Exercise the otherwise discarded branch without requiring an ILP32
      // runtime. This models only branch selection, not a different ABI.
      if (judge) {
        const auto pos = text.find("sizeof(long) == 4");
        check(pos != text.npos,"missing judge branch");
        text.replace(pos,std::string("sizeof(long) == 4").size(),"true");
      }
      { std::ofstream f(temp.path / "output.cpp"); f << text; }
      // The resolver CI SDK supplies libraries, not a clang++ on PATH.
      // Always run the oracle with GCC and the configured build compiler.
      for (const auto& compiler : {gcc,std::string(argv[2])}) {
        ProcessSpec spec; spec.cwd = temp.path; spec.timeout = std::chrono::seconds(20);
        spec.argv = {compiler,"-std=c++23","output.cpp","-o","run"};
        auto built = take(runProcess(spec));
        check(!built.timedOut && built.exitCode == 0,std::string(compiler) + " build: " + built.stderrText);
        spec.argv = {pathToUtf8(temp.path / "run")}; spec.timeout = std::chrono::seconds(5);
        auto ran = take(runProcess(spec));
        check(!ran.timedOut && !ran.signal && ran.exitCode == exitCode && ran.stdoutText.empty() && ran.stderrText.empty(),
              std::string(compiler) + " unexpected exit/output");
      }
    };
    const auto root = std::filesystem::path(DISTILL_PRUNE_CASES);
    for (bool assume : {true,false}) {
      const auto mode = assume ? " A11" : " no-A11";
      run(std::string("reported pasted using") + mode,[&] {
        auto text = read(root / "member_dependent_using/main.cpp");
        auto [m,r,out] = analyze(text,assume);
        verify(text,0); verify(out,0); verify(text,1,true); verify(out,1,true);
        unit(m,r,"Base::hidden_hook",true);
      });
      run(std::string("reported declaration effect") + mode,[&] {
        auto text = read(root / "member_declaration_effect/main.cpp");
        auto [m,r,out] = analyze(text,assume);
        verify(text,0); verify(out,0); verify(text,7,true);
        if (!assume) verify(out,7,true); // Friend injection is excluded by A11.
        unit(m,r,"trigger",!assume); unit(m,r,"Carrier::dormant",!assume);
      });
      // All of these declarations carry expanded names outside expressions,
      // or expressions nested in type/template/constraint traversal.
      for (const auto& [name,decl] : std::vector<std::pair<std::string,std::string>>{
          {"using overloads","template<class T> struct Use : T { using T::JOIN(hidden_,hook); };"},
          {"using pack","template<class... T> struct Use : T... { using T::JOIN(hidden_,hook)...; };"},
          {"using member template","template<class T> struct Use : T { using T::JOIN(hidden_,hook); };"},
          {"dependent friend","template<class T> struct Use { friend Parent T::JOIN(hidden_,hook)(); };"},
          {"template header decltype","template<class T, decltype(&T::JOIN(hidden_,hook)) P = &T::JOIN(hidden_,hook)> struct Use {};"},
          {"default template type","template<class T, class U = decltype(T::JOIN(hidden_,hook)())> struct Use {};"},
          {"requires clause","template<class T> requires requires { T::JOIN(hidden_,hook)(); } struct Use {};"},
          {"dependent base","template<class T> struct Use : decltype(T::JOIN(hidden_,hook)()) {};"}}) {
        run(name + mode,[&] {
          std::string text = "#define JOIN(a,b) a##b\nstruct Parent {};\n"
              "struct Base { static Parent hidden_hook(){return {};} int unrelated(){return 0;} };\n" +
              decl + "\ntemplate<class T> int selected(){if constexpr (sizeof(long) == 4) return sizeof(Use<T>); return 0;}\n"
              "int main(){(void)selected<Base>();}\n";
          if (name == "using overloads")
            text.insert(text.find("int unrelated"),"static Parent hidden_hook(int){return {};} ");
          if (name == "using member template")
            text.insert(text.find("static Parent"),"template<class U> ");
          auto [m,r,out] = analyze(text,assume);
          unit(m,r,"Base::hidden_hook",true); unit(m,r,"Base::unrelated",false);
          verify(text,0,true); verify(out,0,true);
        });
      }
      run(std::string("qualified void friend") + mode,[&] {
        const std::string text = "#define JOIN(a,b) a##b\n"
            "struct Base {void hidden_hook(){} int unrelated(){return 0;}};\n"
            "template<class T> struct Use {friend void T::JOIN(hidden_,hook)();};\n"
            "template<class T> int selected(){if constexpr (sizeof(long) == 4) return sizeof(Use<T>); return 0;}\n"
            "int main(){(void)selected<Base>();}\n";
        auto [m,r,out] = analyze(text,assume);
        unit(m,r,"Base::hidden_hook",true); unit(m,r,"Base::unrelated",false);
        verify(text,0,true); verify(out,0,true);
      });
      run(std::string("typename using and using enum") + mode,[&] {
        const std::string text = "#define JOIN(a,b) a##b\n"
            "struct Base { using hidden_type=int; enum class hidden_enum { value }; int unrelated(){return 0;} };\n"
            "template<class T> struct Use : T { using typename T::JOIN(hidden_,type); };\n"
            "template<class T> struct EnumUse { using enum Base::JOIN(hidden_,enum); };\n"
            "template<class T> int selected(){if constexpr (sizeof(long) == 4) return sizeof(Use<T>)+sizeof(EnumUse<T>); return 0;}\n"
            "int main(){(void)selected<Base>();}\n";
        auto [m,r,out] = analyze(text,assume);
        bool recorded = false;
        for (const auto& f : m.families) if (f.label == "Use") recorded |= f.dependentNames.contains("hidden_type");
        check(recorded,"missing expanded typename using name");
        unit(m,r,"Base::unrelated",false); verify(text,0,true); verify(out,0,true);
      });
      for (const auto& declaration : std::vector<std::string>{
          "void dormant(typename Source<N>::type);",
          "typename Source<N>::type dormant();",
          "void dormant(int = sizeof(Source<N>));",
          "void dormant() noexcept(sizeof(Source<N>) > 0);",
          "void dormant() requires(sizeof(Source<N>) > 0);",
          "template<class U = typename Source<N>::type> void dormant();",
          "template<class U> typename Source<N>::type dormant(U);"}) {
        run(declaration + mode,[&] {
          std::string signature = declaration;
          const auto name = signature.find("Source");
          check(name != signature.npos,"missing signature source");
          signature.replace(name,6,"JOIN(Sour,ce)");
          const std::string text = "#define JOIN(a,b) a##b\nint effect(){return 7;}\n"
              "template<int N> struct Source {using type=int; static inline int state=effect();};\n"
              "template<int N> struct Carrier {" + signature + " int unrelated(){return 0;} };\n"
              "void trigger(){Carrier<0> c;} int main(){}\n";
          auto [m,r,out] = analyze(text,assume);
          unit(m,r,"trigger",true); unit(m,r,"Carrier::dormant",true); unit(m,r,"Carrier::unrelated",false);
          verify(out,0);
        });
      }
      run(std::string("using reverse effect closure") + mode,[&] {
        const std::string text = "#define JOIN(a,b) a##b\nint effect(){return 0;}\n"
            "template<int N> struct Source {static inline int state=effect();};\n"
            "struct Base {void hidden_hook(){(void)Source<0>::state;} int unrelated(){return 0;} };\n"
            "template<class T> struct Use : T {using T::JOIN(hidden_,hook);};\n"
            "template<class T> void trigger(){Use<T> x;} int main(){}\n";
        auto [m,r,out] = analyze(text,assume);
        unit(m,r,"trigger",true); unit(m,r,"Base::hidden_hook",true); unit(m,r,"Base::unrelated",false);
        verify(out,0);
      });
    }
    std::cout << passed << " independent review cases passed, " << failed << " failed\n";
    return failed ? 1 : 0;
  } catch (const Error& e) { std::cerr << e.message << '\n'; return 1; }
}
