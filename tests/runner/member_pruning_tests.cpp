// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/prune/extract.h"
#include "core/render/render.h"
#include "core/report/prune.h"
#include "core/path.h"
#include "core/temporary.h"
#include <algorithm>
#include <fstream>
#include <cstdlib>
#include <iostream>

namespace {
using namespace distill;
template<class T> T take(Result<T> r) { if (!r.ok()) throw Error{r.error().message}; return std::move(r.value()); }
void require(bool value, const std::string& message) { if (!value) throw Error{message}; }
}
int main(int argc, char** argv) {
  if (argc != 2) return 1;
  try {
    TemporaryDirectory temp(".member-pruning-",std::filesystem::current_path());
    auto write = [&](const char* name, const std::string& text) { std::ofstream f(temp.path / name); f << text; };
    write("main.cpp", "int main(){}\n");
    ProbeOptions po; po.temporaryRoot = temp.path;
    const std::string gcc = std::getenv("DISTILL_TEST_GXX") ? std::getenv("DISTILL_TEST_GXX") : "g++";
    auto command = take(parseCompileCommand({gcc,"-std=c++23","main.cpp"},temp.path));
    auto config = take(probeConfiguration(command,"D3e tests",po));
    PruneWorkerOptions worker; worker.executable = std::filesystem::absolute(argv[1]); worker.temporaryRoot = temp.path;
    auto analyze = [&](const std::string& text, bool memberPruning = true, bool assume = true, std::vector<std::string> keep = {}, bool expectRefusal = false) {
      ResolveResult flat; flat.text = text;
      for (std::size_t i = 0, line = 1; i < text.size(); ++i) if (!i || text[i-1] == '\n') flat.lineMap.push_back(SourceLocation{"main.cpp",line++});
      auto model = take(preparePruning(config,{},flat)); model.assumeLibrarySpelledMembers = memberPruning; model.assumeInstantiationEffects = assume;
      model.keepNames.insert(keep.begin(),keep.end());
      model = take(extractPruning(model,worker));
      auto retained = take(computeRetention(model));
      std::string reasons; for (const auto& why : retained.disabledReasons) reasons += why + '\n';
      require(reasons.empty() != expectRefusal,"unexpected refusal state: " + reasons);
      const auto output = expectRefusal ? text : take(renderSource(model,&retained)).provenance.text;
      if (!expectRefusal) { take(renderingInvariant(model,&retained,output)); take(staticNameInvariant(model,retained,output)); }
      return std::tuple{model,retained,output};
    };
    auto verify = [&](const std::string& output) {
      write("output.cpp",output);
      for (const auto& compiler : {gcc,std::string("clang++")}) {
        if (!resolveExecutable(compiler,temp.path).ok()) continue;
        ProcessSpec spec; spec.cwd = temp.path; spec.timeout = std::chrono::seconds(30);
        spec.argv = {compiler,"-std=c++23","-isystem",pathToUtf8(temp.path),"output.cpp","-o","run"};
        auto compile = take(runProcess(spec));
        require(!compile.timedOut && compile.exitCode == 0,"compile: " + compile.stderrText + "\n" + output);
        spec.argv = {pathToUtf8(temp.path / "run")};
        auto run = take(runProcess(spec));
        require(!run.timedOut && run.exitCode == 0,"run: " + run.stderrText);
      }
    };
    auto unit = [&](const PruningModel& m, const PruningResult& r, std::string_view name, bool expected, std::string_view reason = "") {
      bool found = false;
      for (FamilyId i = 0; i < m.families.size(); ++i) if (m.families[i].label == name) {
        found = true; require(r.families[i].kept == expected,std::string(name) + ": " + r.families[i].reason);
        if (!reason.empty()) require((r.families[i].reason + m.families[i].memberBlocker).find(reason) != std::string::npos,
                                     std::string(name) + " missing " + std::string(reason) + ": " + r.families[i].reason);
      }
      require(found,"missing unit " + std::string(name));
    };
    unsigned cases = 0;
    {
      {
        auto [m,r,out] = analyze(
          "int only_live(){return 0;} int only_dead(){return 8;} struct S { int data=2; using Type=int; struct Nested { int drop_nested(){return 0;} };\n"
          "[[nodiscard]] int discard_me(int x=only_dead()); int discard_me(double);\n"
          "template<class T=int> T discard_template(T x=T{}) {return x;} int live(int=only_live()){return data;} };\n"
          "int S::discard_me(int x){return x;} int S::discard_me(double){return 9;}\n"
          "template<class T> struct Box { template<class U> int drop_outer(U x=U{}); };\n"
          "template<class T> template<class U> int Box<T>::drop_outer(U x){return x;}\n"
          "template<> template<> int Box<int>::drop_outer<int>(int x){return x;}\n"
          "int main(){Box<int> b; (void)b; return S{}.live()-2;}\n",true);
        unit(m,r,"S::discard_me",false); unit(m,r,"S::discard_template",false); unit(m,r,"S::Nested::drop_nested",false);
        unit(m,r,"only_live",true); unit(m,r,"only_dead",false); unit(m,r,"Box::drop_outer",false);
        require(out.find("nodiscard") == out.npos,"orphan leading attribute"); verify(out); ++cases;
      }
      {
        auto [m,r,out] = analyze(
          "struct Base { virtual int vname(){return 1;} };\n"
          "struct S { S(){} ~S(){} operator int(){return 0;} int operator()(){return 0;} int vname(){return 0;}\n"
          "int __reserved(){return 0;} int begin(){return 0;} int end(){return 0;} int get(){return 0;}\n"
          "int spelled(){return 0;} int recursive_only(){return recursive_only();} };\n"
          "int main(){S s; return s.spelled();}\n",true);
        unit(m,r,"S::vname",true,"rule 2"); unit(m,r,"S::__reserved",true,"rule 3");
        unit(m,r,"S::begin",true,"rule 4"); unit(m,r,"S::get",true,"rule 4");
        unit(m,r,"S::spelled",true); unit(m,r,"S::recursive_only",false);
        unit(m,r,"S::S",true,"rule 1"); unit(m,r,"S::~S",true,"rule 1"); verify(out); ++cases;
      }
      {
        auto [m,r,out] = analyze(
          "struct S { int dep_probe(){return 0;} int pinned(){return 0;} int forced(){return 0;}\n"
          "// distill: keep\nint annotated(){return 0;} int rooted(){static int v=helper();return v;}\n"
          "static int helper(){return 0;} };\n"
          "template<class T> int call(T t){return t.dep_probe();}\nint main(){S s;return call(s);}\n",true);
        unit(m,r,"S::dep_probe",true); unit(m,r,"S::annotated",true,"rule 7"); unit(m,r,"S::rooted",true,"rule 7");
        unit(m,r,"S::pinned",false); verify(out); ++cases;
      }
      {
        auto [m,r,out] = analyze(
          "template<class T> struct S { int untouched(){return 0;} };\n"
          "extern template struct S<int>; template struct S<long>; int main(){S<char> s;(void)s;}\n",true);
        unit(m,r,"S::untouched",true,"rule 8"); verify(out); ++cases;
      }
      {
        auto [m,r,out] = analyze(
          "template<class T> struct Partial {}; template<class T> struct Partial<T*> {int preserve(){return 0;}};\n"
          "extern template struct Partial<int*>; template struct Partial<long*>; int main(){Partial<char*> p;(void)p;}\n");
        bool found = false;
        for (const auto& f : m.families) if (f.memberName == "preserve") {
          found = true; require(f.memberBlocker.find("rule 8") != f.memberBlocker.npos,"partial pattern lost explicit-instantiation protection");
        }
        require(found,"partial specialization member missing"); verify(out); ++cases;
      }
      {
        auto [m,r,out] = analyze(
          "template<class T> struct Partial {}; template<class T> struct Partial<T*> {int discard_partial(){return 0;}};\n"
          "int main(){Partial<int*> p;(void)p;}\n");
        bool found = false;
        for (FamilyId id = 0; id < m.families.size(); ++id) if (m.families[id].memberName == "discard_partial") {
          found = true; require(!r.families[id].kept,"lazy partial-specialization instance pinned the written pattern");
        }
        require(found,"partial specialization member missing"); verify(out); ++cases;
      }
      {
        auto [m,r,out] = analyze(
          "int effect(){return 7;} template<int> struct Effect { static inline int x=effect(); };\n"
          "struct S { int last_trigger(){return Effect<0>::x;} int unrelated(){return 0;} }; int main(){S s;(void)s;}\n",true);
        unit(m,r,"S::last_trigger",true,"instantiation effect"); unit(m,r,"S::unrelated",false); verify(out); ++cases;
      }
      {
        auto [m,r,out] = analyze(
          "struct Dead {int a; int abandoned(){return 0;} int b; int spare(){return 0;} int c;};\n"
          "int main(){}\n",true);
        unit(m,r,"Dead",false); unit(m,r,"Dead::abandoned",false);
        require(out.find("Dead") == out.npos,"dead record leaked structural fragments"); verify(out); ++cases;
      }
      {
        auto [m,r,out] = analyze(
          "struct Dormant {int size(){return 0;} int spare(){return 0;}};\n"
          "struct Active {int size(){return 0;}}; int main(){return Active{}.size();}\n",true);
        unit(m,r,"Dormant",false); unit(m,r,"Dormant::size",false); unit(m,r,"Active::size",true);
        verify(out); ++cases;
      }
      {
        auto [m,r,out] = analyze(
          "struct S {void unique_member();}; struct F {friend void S::unique_member();};\n"
          "void S::unique_member(){} int main(){F f;S s;(void)f;(void)s;}\n",true);
        unit(m,r,"S::unique_member",true);
        require(out.find("friend void S::unique_member") != out.npos,"qualified friend declaration removed");
        verify(out); ++cases;
      }
      {
        auto [m,r,out] = analyze(
          "struct S {int mixed(), payload; static int static_mixed(), static_payload;};\n"
          "int S::static_payload=3; int main(){S s{};s.payload=2;\n"
          "return sizeof(S)!=sizeof(int) || s.payload!=2 || S::static_payload!=3;}\n",true);
        unit(m,r,"S::mixed",true,"rule 7"); unit(m,r,"S::static_mixed",true,"rule 7"); verify(out); ++cases;
      }
      {
        auto [m,r,out] = analyze(
          "struct Carrier{}; struct S {template<class T> int dormant(T){return 0;}};\n"
          "int dead(){return S{}.dormant(Carrier{});} int main(){S s;Carrier c;(void)s;(void)c;}\n",true);
        unit(m,r,"S::dormant",false); unit(m,r,"dead",false); verify(out); ++cases;
      }
      {
        auto [m,r,out] = analyze(
          "struct Root { virtual int hook(){return 0;} };\n"
          "template<class B> struct Dep : B { int hook(){return 0;} int drop(){return 0;} };\n"
          "struct Override final : Root { int hook() override final {return 0;} int drop(){return 0;} };\n"
          "int main(){Dep<Root> d; Override o; (void)d;(void)o;}\n",true);
        unit(m,r,"Dep::hook",true,"rule 1"); unit(m,r,"Override::hook",true,"rule 1");
        unit(m,r,"Dep::drop",false); verify(out); ++cases;
      }
      {
        auto [m,r,out] = analyze(
          "struct S { static void attr() __attribute__((used)) {} int named_pin(){return 0;}\n"
          "int assembly(){asm(\"\"); return 0;} int main(){return 0;} }; int main(){S s;(void)s;}\n",
          true,true,{"named_pin"});
        unit(m,r,"S::attr",true,"rule 7"); unit(m,r,"S::named_pin",true,"rule 7");
        unit(m,r,"S::assembly",true,"rule 7"); unit(m,r,"S::main",true,"name: main"); verify(out); ++cases;
      }
      {
        auto [m,r,out] = analyze(
          "struct Tag {friend auto supplied(Tag);};\n"
          "template<class T> struct Inject {friend auto supplied(T){return 7;}};\n"
          "struct S {void dormant(){(void)sizeof(Inject<Tag>);} int unrelated(){return 0;}};\n"
          "template<class T> concept Ready=requires(T t){supplied(t);};\n"
          "int main(){S s;(void)s; return !Ready<Tag>;}\n",true,false);
        unit(m,r,"S::dormant",true,"instantiation effect"); unit(m,r,"S::unrelated",false); verify(out); ++cases;
      }
      {
        auto [m,r,out] = analyze(
          "template<class T> struct S {int inst_member(){return 0;} int unneeded(){return 0;}};\n"
          "template int S<int>::inst_member(); int main(){S<int> s;(void)s;}\n",true);
        unit(m,r,"S::inst_member",true); unit(m,r,"S::unneeded",false); verify(out); ++cases;
      }
      {
        auto [m,r,out] = analyze(
          "#define JOIN(a,b) a##b\n#define DECL int made_by_macro(){return 0;}\n"
          "struct S { DECL int paste_target(){return 0;}\n"
          "#pragma GCC diagnostic push\nint with_directive(){\n#pragma GCC diagnostic pop\nreturn 0;} };\n"
          "int main(){return S{}.JOIN(paste_,target)();}\n",true);
        unit(m,r,"S::made_by_macro",true,"rule 7"); unit(m,r,"S::paste_target",true,"AST:");
        unit(m,r,"S::with_directive",true,"rule 7"); verify(out); ++cases;
      }
    }
    {
      auto [m,r,out] = analyze(
          "class S { public: int discard_public(int n=3){return n;} private: int discard_private(){return 9;}\n"
          "protected: template<class T> int discard_protected(T t){return t;} public: int live(){return 0;} };\n"
          "int main(){return S{}.live();}\n");
      unit(m,r,"S::discard_public",false); unit(m,r,"S::discard_private",false); unit(m,r,"S::discard_protected",false);
      require(out.find("public:") != out.npos && out.find("private:") != out.npos && out.find("protected:") != out.npos,"access specifier removed");
      verify(out); ++cases;
    }
    {
      auto [m,r,out] = analyze(
          "struct S { int overload(int){return 0;} int overload(double){return 1;} }; int main(){return S{}.overload(0);}\n");
      unit(m,r,"S::overload",true);
      const auto report = take(renderPruningReport(m,r));
      require(report.find("member units kept=1 removed=0") != report.npos,"overloads reported as multiple units");
      verify(out); ++cases;
    }
    {
      auto [m,r,out] = analyze(
          "#define JOIN(a,b) a##b\nstruct S {int dep_probe(){return 0;} static int scope_probe(){return 0;}};\n"
          "template<class T> int dormant(T t){return t.JOIN(dep_,probe)()+T::JOIN(scope_,probe)();}\n"
          "int main(){S s;(void)s;}\n",true,true,{"dormant"});
      unit(m,r,"S::dep_probe",true,"dependent name:"); unit(m,r,"S::scope_probe",true,"dependent name:"); verify(out); ++cases;
    }
    // Synthetic system headers isolate raw/expanded library spelling and cover
    // uninstantiated library-only callers.
    write("memory", "#include <utility>\n#define LIBRARY_PROTOCOL allocate\n#if 0\nvoid inactive_name();\n#endif\n"
                    "template<class T> int use_allocator(T t){return t.allocate();}\n");
    write("utility", "// Strings and comments must not protect no_hit.\nconst char* text=\"no_hit\";\n"
                     "#if 0\nvoid transitive_name();\n#endif\n#define EOF_NAME eof_name");
    auto originalConfig = config;
    config.probe.searchLists.directories.insert(config.probe.searchLists.directories.begin(),
        {temp.path,IncludeKind::system});
    {
      auto [m,r,out] = analyze("#include <memory>\nstruct Alloc { int allocate(){return 0;} int inactive_name(){return 0;} int transitive_name(){return 0;} int eof_name(){return 0;} int no_hit(){return 0;} }; int main(){Alloc a;(void)a;}\n");
      unit(m,r,"Alloc::allocate",true,"rule 4"); unit(m,r,"Alloc::inactive_name",true,"rule 4");
      unit(m,r,"Alloc::transitive_name",true,"rule 4"); unit(m,r,"Alloc::eof_name",true,"rule 4"); unit(m,r,"Alloc::no_hit",false);
      require(r.assumptions.contains("A12"),"missing A12"); verify(out); ++cases;
    }
    {
      auto [m,r,out] = analyze("#include <memory>\nstruct Alloc { int allocate(){return 0;} int no_hit(){return 0;} }; int main(){return use_allocator(Alloc{});}\n");
      unit(m,r,"Alloc::allocate",true); unit(m,r,"Alloc::no_hit",false); verify(out); ++cases;
    }
    write("utility", "#define UNUSED_JOIN(a,b) a ## b\n#if 0\n#define JOIN(a,b) a %:%: b\n#endif\n");
    {
      auto [m,r,out] = analyze("#include <memory>\nstruct Alloc {int unused(){return 0;}}; int dead(){return 0;} int main(){Alloc a;(void)a;}\n");
      unit(m,r,"Alloc::unused",false); unit(m,r,"dead",false);
      require(r.memberDisabledReasons.empty(),"unexpanded header ## disabled members");
      require(r.assumptions.contains("A12"),"missing A12 with unexpanded ##");
      verify(out); ++cases;
    }
    write("memory", "#define CALL(x) x.do_ ## thing()\n"
                    "template<class T> int system_call(T x){return CALL(x);}\n");
    {
      // No instantiation/AST reference: the final preprocessor tokens alone
      // must protect the pasted dependent name in the library template.
      auto [m,r,out] = analyze("#include <memory>\nstruct S {int do_thing(){return 0;} int unused(){return 0;}}; int main(){S s;(void)s;}\n");
      require(m.libraryMemberNames.contains("do_thing"),"missing system paste result");
      unit(m,r,"S::do_thing",true,"rule 4"); unit(m,r,"S::unused",false); verify(out); ++cases;
    }
    {
      auto [m,r,out] = analyze("#include <memory>\nstruct S {int do_thing(){return 0;} int unused(){return 0;}}; int main(){return system_call(S{});}\n");
      unit(m,r,"S::do_thing",true); unit(m,r,"S::unused",false); verify(out); ++cases;
    }
    write("memory", "#define LIB_CALL(x, name) x.name()\n"
                    "#define LIB_JOIN(a,b) a ## b\n");
    {
      // Arguments and the invocation are user-spelled. Argument expansion
      // locations must still lead back to the system macro's definition.
      auto [m,r,out] = analyze("#include <memory>\nstruct S {int arg_member(){return 0;} int user_paste(){return 0;} int unused(){return 0;}};\n"
          "#define USER_NAME user_paste\n"
          "template<class T> int dormant(T x){return LIB_CALL(x, arg_member)+LIB_CALL(x, USER_NAME);}\n"
          "int main(){S s;(void)s;}\n");
      require(m.libraryMemberNames.contains("arg_member"),"missing user argument of system macro");
      require(m.libraryMemberNames.contains("user_paste"),"missing expanded user argument of system macro");
      unit(m,r,"S::arg_member",true,"rule 4"); unit(m,r,"S::user_paste",true,"rule 4");
      unit(m,r,"dormant",false); unit(m,r,"S::unused",false); verify(out); ++cases;
    }
    {
      auto [m,r,out] = analyze("#include <memory>\nstruct S {int user_site(){return 0;} int unused(){return 0;}};\n"
          "template<class T> int dormant(T x){return x.LIB_JOIN(user_,site)();}\n"
          "int main(){S s;(void)s;}\n");
      require(m.libraryMemberNames.contains("user_site"),"missing system paste at user invocation");
      unit(m,r,"S::user_site",true,"rule 4"); unit(m,r,"dormant",false);
      unit(m,r,"S::unused",false); verify(out); ++cases;
    }
    write("memory", "template<class T> int system_call(T x){return USER_CALL(x);}\n");
    {
      auto [m,r,out] = analyze("#define USER_CALL(x) x.context_ ## member()\n#include <memory>\n"
          "struct S {int context_member(){return 0;} int unused(){return 0;}}; int main(){S s;(void)s;}\n");
      require(m.libraryMemberNames.contains("context_member"),"missing user macro paste in system context");
      unit(m,r,"S::context_member",true,"rule 4"); unit(m,r,"S::unused",false); verify(out); ++cases;
    }
    write("memory", "#define PROTOCOL sp\\\nlit_name\n");
    {
      auto [m,r,out] = analyze("#include <memory>\nstruct S {int split_name(){return 0;} int unused(){return 0;}}; int main(){S s;(void)s;}\n");
      unit(m,r,"S::split_name",true,"rule 4"); unit(m,r,"S::unused",false); verify(out); ++cases;
    }
    write("memory", "#pragma GCC system_header\n#define PROTOCOL marked_name\n");
    config.probe.searchLists.directories.front().kind = IncludeKind::quote;
    {
      auto [m,r,out] = analyze("#include \"memory\"\nstruct S {int marked_name(){return 0;} int unused(){return 0;}}; int main(){S s;(void)s;}\n");
      unit(m,r,"S::marked_name",true,"rule 4"); unit(m,r,"S::unused",false); verify(out); ++cases;
    }
    config.probe.searchLists.directories.front().kind = IncludeKind::system;
    write("memory", "#define PROTOCOL allocate\n");
    {
      auto [m,r,out] = analyze("#include <memory>\nint effect(){return 0;} template<int> struct Effect {static inline int x=effect();}; struct Alloc {int allocate(){return Effect<0>::x;}}; int dead(){return 0;} int main(){Alloc a;(void)a;}\n",true,true,{},true);
      require(std::any_of(r.disabledReasons.begin(),r.disabledReasons.end(),[](const auto& why){return why.find("unbounded system instantiation effects through Alloc::allocate") != why.npos;}),"effectful library member must refuse"); ++cases;
    }
    write("memory", "#if 0\n@\n#endif\n");
    {
      auto [m,r,out] = analyze("#include <memory>\nstruct S {int unused(){return 0;}}; int dead(){return 0;} int main(){S s;(void)s;}\n");
      require(!r.memberDisabledReasons.empty(),"incomplete token inventory did not disable members");
      require(out.find("unused") != out.npos,"incomplete inventory removed member"); unit(m,r,"dead",false); verify(out); ++cases;
    }
    config = originalConfig;
    {
      auto [m,r,out] = analyze("struct S {int unused(){return 0;}}; int dead(){return 0;} int main(){S s;(void)s;}\n",false);
      require(r.memberDisabledReasons == std::vector<std::string>{"--no-assume=library-spelled-members"},"flag reason missing");
      require(!r.assumptions.contains("A12"),"disabled A12 reported");
      require(out.find("unused") != std::string::npos,"flag removed a member");
      unit(m,r,"dead",false); verify(out); ++cases;
    }
    std::cout << cases << " member pruning cases passed (available GCC and Clang compilation/runtime)\n";
    return 0;
  } catch (const Error& e) { std::cerr << e.message << '\n'; return 1; }
  catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
