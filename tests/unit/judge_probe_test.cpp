// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.h"
#include "core/profile/probe.h"
#include "core/profile/sha256.h"
#include "core/command/options.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <sstream>
using namespace distill;
namespace {
Profile small() {
  Profile p; p.name = "fixture-gcc15-gnupp23"; p.compiler = {CompilerFamily::gcc,15,2,0};
  p.provenance.contentDigest = detail::sha256("fixture"); p.provenance.selector = "fixture";
  p.probeSourceBytes = p.probeOutputBytes = 16777216;
  p.predefined = {{"OK", Macro{false,{},false,"(1, 2)"}},
    {"LINE_ALIAS", Macro{false,{},false,"INDIRECT"}}, {"INDIRECT", Macro{false,{},false,"__LINE__"}},
    {"PRAGMA", Macro{false,{},false,"_Pragma(\"once\")"}}, {"OPEN", Macro{false,{},false,"("}},
    {"CLOSE", Macro{false,{},false,")"}}, {"FUNC", Macro{true,{"x"},false,"x"}},
    {"PASTE", Macro{false,{},false,"A ## B"}}, {"TEXT", Macro{false,{},false,"\"__LINE__ _Pragma )\""}},
    {"__GNUC__", Macro{false,{},false,"15"}},
    {"PAR1", Macro{false,{},false,"PAR2"}}, {"PAR2", Macro{false,{},false,"("}},
    {"PRAGMA_ALIAS", Macro{false,{},false,"PRAGMA"}}, {"CYCLE", Macro{false,{},false,"CYCLE"}}};
  for (const auto& [n,m] : p.predefined) p.snapshotFacts[n] = {MacroState::known,m,{FactOrigin::compilerBuiltin}};
  return p;
}
JudgeProbe probe(const Profile& p, JudgeProbePart part = {}) {
  auto r = generateJudgeProbe(p,part); CHECK(r.ok());
  if (!r.ok()) throw std::runtime_error(r.error().message);
  return r.value();
}
std::string hex(std::string_view s) {
  std::string result;
  for (unsigned char c : s) { result += "0123456789abcdef"[c>>4]; result += "0123456789abcdef"[c&15]; }
  return result;
}
std::string finish(std::string bytes) {
  std::uint32_t h=2166136261u; std::size_t lines=0;
  for (unsigned char c : bytes) { h=(h^c)*16777619u; if (c=='\n') ++lines; }
  return bytes + "END|" + std::to_string(lines) + "|" + std::to_string(h) + '\n';
}
using Values = std::map<std::string,std::string>;
std::string output(const Profile& p, JudgeProbePart part = {}, Values replacements = {},
                   std::string missing = {}, std::string extra = {}, Values raw = {}) {
  auto g = probe(p,part);
  std::string out = g.header + '\n' + g.manifest + '\n';
  for (const auto& f : g.facts) {
    const auto key = f.category + "|" + f.name;
    if (key == missing) continue;
    std::string value = f.category == "presence" ? (p.predefined.contains(f.name) ? "1" : "0") : "sample";
    if (f.category == "header") value = "1";
    if (!f.unsupported.empty()) value = "unsupported:" + f.unsupported;
    if (replacements.contains(key)) value = replacements.at(key);
    out += key + '|' + (raw.contains(key) ? raw.at(key) : hex(value)) + '\n';
  }
  return finish(out + extra);
}
void failure(const Profile& p, const std::string& got, std::string_view reason) {
  auto r = checkJudgeProbe(p,output(p),{got});
  CHECK(!r.passed); CHECK(r.report.find(reason) != r.report.npos);
}
}
TEST(judge_probe_deterministic_order_and_audited_subset) {
  auto p = small(); auto a = probe(p), b = probe(p);
  CHECK_EQ(a.source,b.source); CHECK_EQ(a.manifest,b.manifest);
  p.probeExpected="untrusted expected bytes"; p.latestObservation="untrusted observation";
  CHECK_EQ(a.source,probe(p).source);
  auto presenceEnd = a.source.find("// presence complete");
  CHECK(presenceEnd < a.source.find("#define")); CHECK(presenceEnd < a.source.find("#include"));
  CHECK(a.source.find("#define __distill_probe_raw") < a.source.find("__has_include(<"));
  CHECK(a.source.find("#ifdef __distill_probe_raw") < presenceEnd);
  auto reason = [&](std::string n) {
    for (const auto& f : a.facts) if (f.category=="expansion" && f.name==n) return f.unsupported;
    return std::string("missing");
  };
  CHECK(reason("OK").empty()); CHECK(reason("TEXT").empty());
  CHECK_EQ(reason("LINE_ALIAS"),"contextual field"); CHECK_EQ(reason("PRAGMA"),"_Pragma");
  CHECK_EQ(reason("OPEN"),"unbalanced parentheses"); CHECK_EQ(reason("CLOSE"),"unbalanced parentheses");
  CHECK_EQ(reason("PAR1"),"unbalanced parentheses"); CHECK_EQ(reason("PRAGMA_ALIAS"),"_Pragma");
  CHECK_EQ(reason("CYCLE"),"recursive expansion");
  CHECK_EQ(reason("FUNC"),"function-like; presence only"); CHECK_EQ(reason("PASTE"),"token construction");
  CHECK(a.outputBound > output(p).size());
  auto before = p.snapshotFacts;
  auto check = checkJudgeProbe(p,output(p),{output(p)});
  CHECK_EQ(p.snapshotFacts.size(),before.size());
  CHECK_EQ(p.snapshotFacts.at("OK").definition,before.at("OK").definition);
  CHECK(check.passed); CHECK(check.checked>0); CHECK(check.skipped>0);
  CHECK(check.report.find("user-reported match")!=check.report.npos);
}
TEST(judge_probe_library_stages_and_budgets) {
  auto p = small(); p.libraries = {"glibc","libstdc++","mingw-w64"};
  auto s = probe(p).source;
  for (const char* h : {"features.h","bits/c++config.h","_mingw.h"}) {
    CHECK(s.find(std::string("__has_include(<")+h+">)") < s.find(std::string("#include <")+h+">"));
    CHECK(s.find(std::string("#include <")+h+">") < s.find("int main()"));
  }
  p.probeSourceBytes=1024; CHECK(!generateJudgeProbe(p).ok());
  p.probeSourceBytes=16777216; p.probeOutputBytes=1024; CHECK(!generateJudgeProbe(p).ok());
  CHECK(!parseJudgeProbePart("0/2").ok()); CHECK(!parseJudgeProbePart("3/2").ok());
  CHECK(!parseJudgeProbePart("1").ok()); CHECK(!parseJudgeProbePart("01/2").ok());
  CHECK(parseJudgeProbePart("2/3").ok());
}
TEST(judge_probe_native_join_and_transitive_alias_invariants) {
  auto p = small();
  p.snapshotFacts["VARIABLE"] = {MacroState::unknown,{}, {FactOrigin::nativeModel}};
  p.snapshotFacts["ALWAYS"] = {MacroState::definedUnknown,{}, {FactOrigin::nativeModel}};
  p.snapshotFacts["INVARIANT"] = {MacroState::known,Macro{false,{},false,"1"},{FactOrigin::nativeModel}};
  p.predefined["NATIVE_ALIAS"] = Macro{false,{},false,"VARIABLE"};
  // Real profiles list every predefined macro in snapshotFacts too; otherwise
  // A5 makes the name undefined and a judge "1" is a contradicted assumption.
  p.snapshotFacts["NATIVE_ALIAS"] = {MacroState::known,p.predefined["NATIVE_ALIAS"],{FactOrigin::compilerBuiltin}};
  auto e = output(p,{},{{"presence|ALWAYS","1"},{"presence|INVARIANT","1"}});
  auto j = output(p,{},{{"presence|ALWAYS","1"},{"presence|INVARIANT","1"},{"presence|VARIABLE","1"},{"expansion|NATIVE_ALIAS","host-specific"}});
  auto r = checkJudgeProbe(p,e,{j}); CHECK(r.passed);
  auto changed = output(p,{},{{"presence|ALWAYS","1"},{"presence|INVARIANT","1"},{"expansion|INVARIANT","2"}});
  CHECK(!checkJudgeProbe(p,e,{changed}).passed);
  changed = output(p,{},{{"presence|ALWAYS","0"},{"presence|INVARIANT","1"}});
  CHECK(!checkJudgeProbe(p,e,{changed}).passed);
  changed = output(p,{},{{"presence|ALWAYS","1"},{"presence|INVARIANT","0"}});
  CHECK(!checkJudgeProbe(p,e,{changed}).passed);
}
TEST(judge_probe_native_expected_marker_is_expected_only_and_model_scoped) {
  auto p = small();
  p.snapshotFacts["VARIABLE"] = {MacroState::unknown,{}, {FactOrigin::nativeModel}};
  p.snapshotFacts["ALWAYS"] = {MacroState::definedUnknown,{}, {FactOrigin::nativeModel}};
  p.snapshotFacts["INVARIANT"] = {MacroState::known,Macro{false,{},false,"1"},{FactOrigin::nativeModel}};
  p.snapshotFacts["ABSENT"] = {MacroState::undefined,{}, {FactOrigin::nativeModel}};
  p.predefined["NATIVE_ALIAS"] = Macro{false,{},false,"VARIABLE"};
  p.snapshotFacts["NATIVE_ALIAS"] = {MacroState::known,p.predefined["NATIVE_ALIAS"],{FactOrigin::compilerBuiltin}};
  const Values markers{{"presence|VARIABLE","native"}, {"presence|ALWAYS","native"},
                       {"expansion|NATIVE_ALIAS","native"}};
  const auto expected = output(p,{}, {}, {}, {}, markers);
  p.probeExpected = expected;
  CHECK(judgeProbeExpected(p).ok());
  for (const auto* value : {"0", "1"}) {
    auto actual = output(p,{},{{"presence|VARIABLE",value},{"presence|ALWAYS","1"},
                              {"expansion|NATIVE_ALIAS","host-dependent"}});
    CHECK(checkJudgeProbe(p,expected,{actual}).passed);
  }
  CHECK(!checkJudgeProbe(p,expected,{output(p)}).passed); // ALWAYS must be present.
  CHECK(!checkJudgeProbe(p,expected,{expected}).passed); // Raw marker is never judge output.
  CHECK(!checkJudgeProbe(p,expected,{output(p,{},{{"presence|ALWAYS","1"},
      {"presence|VARIABLE","2"}})}).passed);
  for (const auto& [key, marker] : markers) {
    auto r = checkJudgeProbe(p,expected,{output(p,{},{{"presence|ALWAYS","1"}}, {}, {}, {{key,marker}})});
    CHECK(!r.passed);
    CHECK(r.report.find("native marker only allowed") != r.report.npos);
  }
  auto parts = output(p,{1,2},{},{},{},markers) + output(p,{2,2},{},{},{},markers);
  CHECK(checkJudgeProbe(p,parts,{output(p,{2,2},{{"presence|ALWAYS","1"}}),
                                output(p,{1,2},{{"presence|ALWAYS","1"}})}).passed);
  // Encoded expansion text "native" is a real value, not the raw marker.
  CHECK(checkJudgeProbe(p,expected,{output(p,{},{{"presence|ALWAYS","1"},
      {"expansion|NATIVE_ALIAS","native"}})}).passed);
  for (const auto* key : {"presence|OK", "expansion|OK", "presence|INVARIANT",
                          "presence|ABSENT", "expansion|VARIABLE", "header|cstdio"}) {
    p.probeExpected = output(p,{}, {}, {}, {}, {{key,"native"}});
    CHECK(!judgeProbeExpected(p).ok());
    CHECK(!checkJudgeProbe(p,*p.probeExpected,{output(p)}).passed);
  }
  const auto source = probe(p).source;
  CHECK(source.find("presence|VARIABLE|native") != source.npos);
  CHECK(source.find("presence|ALWAYS|native") != source.npos);
  CHECK(source.find("expansion|NATIVE_ALIAS|native") != source.npos);
  CHECK(source.find("presence|INVARIANT|native") == source.npos);
  CHECK(source.find("presence|ABSENT|native") == source.npos);
  CHECK(source.find("expansion|VARIABLE|native") == source.npos); // Presence-only reason stays fixed.
  CHECK(source.find("--distill-expected") != source.npos);
  CHECK(probe(small()).source.find("--distill-expected") == std::string::npos);
}
TEST(judge_probe_parts_manifest_complete_missing_duplicate_mixed) {
  auto p = small(); auto expected = output(p,{1,2}) + output(p,{2,2});
  CHECK(checkJudgeProbe(p,expected,{output(p,{2,2}),output(p,{1,2})}).passed);
  CHECK(!checkJudgeProbe(p,expected,{output(p,{1,2})}).passed);
  CHECK(!checkJudgeProbe(p,expected,{output(p,{1,2}),output(p,{1,2})}).passed);
  CHECK(!checkJudgeProbe(p,expected,{output(p,{1,2}),output(p,{2,3})}).passed);
  auto corrupt = output(p,{1,2}); auto start = corrupt.find("PART|"); corrupt[start+9] = 'z';
  CHECK(!checkJudgeProbe(p,expected,{corrupt,output(p,{2,2})}).passed);
  // Re-checksum a misplaced fact: the manifest must still reject it.
  auto part1 = output(p,{1,2}); part1.erase(part1.find("END|"));
  part1 += "assumption|coverage|" + hex("unsupported:wrong part") + '\n';
  CHECK(!checkJudgeProbe(p,expected,{finish(part1),output(p,{2,2})}).passed);
}
TEST(judge_probe_corruption_and_all_failure_classes) {
  auto p = small(); auto valid = output(p);
  failure(p,"","probe failure");
  auto corrupt = valid; corrupt[0]='X'; failure(p,corrupt,"format error");
  corrupt=valid; auto digestAt=corrupt.find(p.provenance.contentDigest); corrupt[digestAt]='z'; failure(p,corrupt,"format error");
  std::string crlf;
  for (char c : valid) { if (c=='\n') crlf+='\r'; crlf+=c; }
  CHECK(checkJudgeProbe(p,valid,{crlf}).passed);
  failure(p,valid.substr(0,valid.size()-1),"format error");
  corrupt=valid; corrupt[corrupt.find("END|")+4]='9'; failure(p,corrupt,"checksum");
  corrupt=valid; corrupt[corrupt.find("presence|")+10]='X'; failure(p,corrupt,"checksum");
  failure(p,output(p,{},{{"expansion|__GNUC__","14"}}),"wrong compiler or option selected");
  failure(p,output(p,{},{{"expansion|OK","different"}}),"fact mismatch");
  failure(p,output(p,{},{{"presence|LOCAL","1"}}),"contradicted assumption");
  failure(p,output(p,{},{{"expansion|LINE_ALIAS","123"}}),"contextual field");
  failure(p,output(p,{},{{"expansion|OK","!probe-failure:value exceeds 256 bytes"}}),"probe failure");
  failure(p,output(p,{},{{"header|cstdio","unsupported:no operator"}}),"probe failure");
  failure(p,output(p,{}, {}, "expansion|OK"),"missing expansion|OK");
  failure(p,output(p,{}, {}, {}, "expansion|EXTRA|"+hex("new")+'\n'),"extra expansion|EXTRA");
  CHECK(checkJudgeProbe(p,"",{valid}).report.find("no expected output shipped for fixture-gcc15-gnupp23; run profiles.yml")!=std::string::npos);
}
TEST(judge_probe_record_names_and_embedded_latest_observation) {
  test::TempDirectory dir; auto p=small(); auto bytes=output(p); auto check=checkJudgeProbe(p,bytes,{bytes});
  auto one=recordJudgeObservation(p,check,{bytes},dir.path,"2026-11-16"); CHECK(one.ok());
  auto two=recordJudgeObservation(p,check,{bytes},dir.path,"2026-11-16"); CHECK(two.ok());
  CHECK_EQ(pathToUtf8(one.value().filename()),p.name+"-2026-11-16-1.txt");
  CHECK_EQ(pathToUtf8(two.value().filename()),p.name+"-2026-11-16-2.txt");
  CHECK(test::read(one.value()).find("scope: custom test of this option, that date")!=std::string::npos);
  ProfileFiles files{{"observations/"+pathToUtf8(one.value().filename()),test::read(one.value())}};
  CHECK(latestJudgeObservation(p,files).find("user-reported match")!=std::string::npos);
  files["observations/"+p.name+"-2026-11-16-10.txt"] = test::read(one.value());
  files["observations/"+p.name+"-2026-11-16-2.txt"] = test::read(two.value());
  CHECK(latestJudgeObservation(p,files).find("-10.txt")!=std::string::npos);
  p.provenance.contentDigest="other"; CHECK_EQ(latestJudgeObservation(p,files),"-");
}
TEST(judge_probe_cli_options_smoke) {
  auto p = parseDistillOptions({"--judge-probe","atcoder","--part","1/2"});
  CHECK(p.ok()); CHECK(p.value().compile.empty()); CHECK_EQ(p.value().part.value(),"1/2");
  auto c = parseDistillOptions({"--judge-check","codeforces","one.txt","--record","observations","two.txt"});
  CHECK(c.ok()); CHECK_EQ(c.value().judgeOutputs.size(),2u);
  for (const auto& args : std::vector<std::vector<std::string>>{
    {"--judge-check","atcoder"},{"--judge-probe","atcoder","g++","main.cpp"},
    {"--judge-probe","atcoder","--record","obs"},{"--judge-check","atcoder","o","--part","1/2"},
    {"--part","1/2","main.cpp"},{"--judge-probe","atcoder","--judge","codeforces"},
    {"--judge-probe","atcoder","--judge-check","codeforces","o"}}) CHECK(!parseDistillOptions(args).ok());
}
TEST(judge_probe_shipped_expected_outputs_match_current_generator) {
  // Validate expected bytes against the current generator. Expected-only native
  // markers cannot be supplied as judge output; the real-output path is tested
  // separately above. Legacy hex expected files remain valid until CI recapture.
  auto catalog = listProfiles();
  CHECK(catalog.ok());
  if (!catalog.ok()) return;
  for (const auto& p : catalog.value().profiles) {
    if (!p.probeExpected) continue;
    auto expected = judgeProbeExpected(p);
    if (!expected.ok()) std::cerr << p.name << ": " << expected.error().message << '\n';
    CHECK(expected.ok());
  }
}
TEST(judge_probe_gcc16_uses_the_audited_special_builtin_registry) {
  auto p = small();
  p.name = "fixture-gcc16-gnupp23";
  p.compiler = {CompilerFamily::gcc, 16, 2, 0};
  const auto generated = probe(p);
  for (const auto& name : specialBuiltins(p.compiler)) {
    CHECK(std::any_of(generated.facts.begin(), generated.facts.end(), [&](const auto& fact) {
      return fact.category == "presence" && fact.name == name;
    }));
    CHECK(std::any_of(generated.facts.begin(), generated.facts.end(), [&](const auto& fact) {
      return fact.category == "builtin" && fact.name == name;
    }));
  }
}

