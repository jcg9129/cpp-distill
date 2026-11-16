// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/prune/extract.h"
#include "core/path.h"
#include "core/temporary.h"
#include <algorithm>
#include <iostream>
namespace {
using namespace distill;
template<class T> T take(Result<T> r) { if (!r.ok()) throw Error{r.error().message}; return std::move(r.value()); }
void check(bool condition, const char* why) { if (!condition) throw Error{why}; }
const PruneFamily& family(const PruningModel& m, std::string_view label) {
  for (const auto& f : m.families) if (f.label == label) return f;
  throw Error{"missing family: " + std::string(label)};
}
PruningModel extract(std::string text, const PruneWorkerOptions& o) {
  Configuration c; c.probe.triple = "x86_64-unknown-linux-gnu"; c.probe.dialect = {"c++20",false,202002};
  ResolveResult flat; flat.text = std::move(text);
  for (std::size_t i = 0, line = 1; i < flat.text.size(); ++i) if (i == 0 || flat.text[i-1] == '\n') flat.lineMap.push_back(SourceLocation{"original.hpp",line++});
  auto m = take(preparePruning(c,{},flat)); return take(extractPruning(m,o));
}
}
int main(int argc, char** argv) {
  if (argc != 2) return 1;
  try {
    TemporaryDirectory temp(".prune-extract-tests-",std::filesystem::current_path());
    PruneWorkerOptions o; o.executable = std::filesystem::absolute(argv[1]); o.temporaryRoot = temp.path;
    std::size_t checks = 0;
    auto m = extract("template<class T> struct Box { void f(); };\ntemplate<class T> void Box<T>::f() { int local=0; (void)local; }\nint unused() { return 0; }\nint main() { Box<int> x; x.f(); }\n",o);
    auto r = take(computeRetention(m)); check(r.disabledReasons.empty(),"ownership unexpectedly disabled");
    const auto& box = family(m,"Box"); check(!box.ranges.empty(),"missing template range");
    check(m.text.substr(box.ranges[0].begin,8) == "template","template prefix missing");
    check(m.text[box.ranges[0].end-1] == ';',"record terminator missing");
    check(!box.names.contains("local") && !box.names.contains("f"),"member/local indexed");
    check(!m.unions.empty(),"out-of-line member not aggregated"); ++checks;
    m = extract("int a, b; struct S {} object; int main(){return b;}\n",o); r = take(computeRetention(m));
    check(r.disabledReasons.empty(),"declaration groups disabled");
    check(!m.unions.empty(),"shared declarators not joined");
    std::size_t bytes = 0; for (const auto& f : m.families) for (auto range : f.ranges) bytes += range.end-range.begin;
    check(bytes <= m.text.size(),"declaration bytes counted twice"); ++checks;
    m = extract("namespace n { int f(int){return 0;} int f(long){return 0;} } int main(){return n::f(1);}\n",o);
    r = take(computeRetention(m)); check(r.disabledReasons.empty(),"overloads disabled");
    check(!m.unions.empty(),"overloads not aggregated"); ++checks;
    m = extract("constexpr int get_value(){return 1;} int active=get_value(); void automatic(){int x=get_value();(void)x;} void stat(){static int x=get_value();} int main(){}\n",o);
    r = take(computeRetention(m)); check(r.disabledReasons.empty(),"storage disabled");
    check(!family(m,"active").root.empty(),"constexpr call incorrectly inert");
    check(family(m,"automatic").root.empty(),"automatic local rooted");
    check(!family(m,"stat").root.empty(),"static local not rooted"); ++checks;
    m = extract("#define JOIN_I(a,b) a##b\n#define JOIN(a,b) JOIN_I(a,b)\ntemplate<class T> void run(T x){JOIN(custo,mize)(x);}\nnamespace later { template<class T> void customize(T){} }\nint main(){}\n",o);
    r = take(computeRetention(m)); check(r.disabledReasons.empty(),"user-only paste disabled");
    check(family(m,"run").dependentNames.contains("customize"),"synthesized dependent name missing"); ++checks;
    m = extract("#undef __GNUC__\n#define __GNUC__ 7\nint x=__GNUC__; int main(){}\n",o);
    r = take(computeRetention(m)); check(r.disabledReasons.empty(),"source macro override ignored"); ++checks;
    m = extract("#define CAT_I(a,b) a##b\n#define CAT(a,b) CAT_I(a,b)\nint x=CAT(__GNUC,__); int main(){}\n",o);
    r = take(computeRetention(m)); check(!r.disabledReasons.empty(),"synthesized predefined name accepted");
    check(std::any_of(m.expansions.begin(),m.expansions.end(),[](const auto& x) { return x.name == "__GNUC__" && x.predefined; }),"predefined rescan fact missing"); ++checks;
    m = extract("template<class T> void run(){T::opaque();} int main(){}\n",o);
    r = take(computeRetention(m)); check(r.disabledReasons.empty(),"dependent qualified lookup disabled"); ++checks;
    m = extract("[[nodiscard]] int unused(){return 1;}\n[[maybe_unused]] int a,b;\nalignas(16) int aligned;\n[[gnu::always_inline]] inline int inlined(){return 0;}\nint main(){}\n",o);
    r = take(computeRetention(m)); check(r.disabledReasons.empty(),"ordinary attributes disabled");
    check(m.text.substr(family(m,"unused").ranges[0].begin,13) == "[[nodiscard]]","leading standard attribute not owned");
    check(m.text.substr(family(m,"a").ranges[0].begin,16) == "[[maybe_unused]]","shared leading attribute not owned");
    check(m.text.substr(family(m,"aligned").ranges[0].begin,7) == "alignas","leading alignment not owned");
    check(m.text.substr(family(m,"inlined").ranges[0].begin,5) == "[[gnu","scoped attribute not owned"); ++checks;
    m = extract("#define INLINE __attribute__((always_inline))\nINLINE inline int f(){return 0;} int main(){}\n",o);
    r = take(computeRetention(m)); check(r.disabledReasons.empty(),"attribute macro disabled");
    check(!family(m,"f").pinned.empty(),"attribute macro not pinned"); ++checks;
    m = extract("int x __attribute__((used)); int main(){}\n",o); r = take(computeRetention(m));
    check(!r.disabledReasons.empty(),"opaque attribute accepted"); ++checks;
    m = extract("int main(){asm(\"\");}\n",o); r = take(computeRetention(m));
    check(!r.disabledReasons.empty(),"opaque asm accepted"); ++checks;
    {
      Configuration c; c.probe.triple = "x86_64-unknown-linux-gnu"; c.probe.dialect = {"c++20",false,202002};
      ResolveResult flat; flat.text = "// distill: keep\nint preserved(){return 1;} int main(){}\n";
      auto input = take(preparePruning(c,{},flat));
      const auto newline = input.text.find('\n'); input.text.replace(0,newline,newline,' ');
      m = take(extractPruning(input,o)); r = take(computeRetention(m));
      check(r.disabledReasons.empty() && !m.annotations.empty(),"keep metadata lost after stripping comment");
      check(family(m,"preserved").names.contains("preserved"),"kept declaration missing"); ++checks;
    }
    m = extract("int broken(; int main(){}\n",o); r = take(computeRetention(m));
    check(!r.disabledReasons.empty() && m.families.empty(),"recovery AST analyzed"); ++checks;
    std::cout << checks << " extraction model checks passed\n"; return 0;
  } catch (const Error& e) { std::cerr << e.message << '\n'; return 1; }
  catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
