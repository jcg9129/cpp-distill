// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/prune/extract.h"
#include "core/path.h"
#include "core/temporary.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
namespace distill {
namespace {
bool supportedPragma(const Directive& d) {
  const auto& t = d.tokens;
  if (t.empty()) return false;
  // Match complete pragma names, never arbitrary vendor prefixes. All source
  // directives survive and a family crossing one is pinned by the core.
  if (t[0].text == "once") return t.size() == 1; // Normally rewritten by D2.
  if (t[0].text == "pack" || t[0].text == "message" || t[0].text == "warning") return true;
  if (t.size() < 2 || (t[0].text != "GCC" && t[0].text != "clang")) return false;
  if (t[1].text == "diagnostic") return true;
  if (t[0].text != "GCC") return false;
  if (t[1].text == "optimize" || t[1].text == "target") return true;
  return t.size() == 2 && (t[1].text == "push_options" || t[1].text == "pop_options" || t[1].text == "reset_options");
}
} // namespace
Result<PruningModel> preparePruning(const Configuration& c, const InlineResult&, const ResolveResult& flat,
                                   const Profile* judge, const std::vector<std::string>& keep) {
  PruningModel m; m.text = flat.text; m.lineMap = flat.lineMap; m.dialect = c.probe.dialect.name;
  m.disabledReasons = flat.disabledReasons; m.unknownInUserCode = flat.unknownInUserCode;
  m.keepNames.insert(keep.begin(),keep.end());
  auto annotations = captureKeepOffsets(m.text); if (!annotations.ok()) return annotations.error();
  m.keepOffsets = flat.keepOffsets;
  m.keepOffsets.insert(m.keepOffsets.end(),annotations.value().begin(),annotations.value().end());
  std::sort(m.keepOffsets.begin(),m.keepOffsets.end());
  m.keepOffsets.erase(std::unique(m.keepOffsets.begin(),m.keepOffsets.end()),m.keepOffsets.end());
  for (const auto& why : c.command.pruningAllowed().reasons) m.disabledReasons.push_back(why);
  m.judgeProfile = judge != nullptr; m.judgeComplete = judge && judge->reproduced;
  if (judge) {
    for (const auto& [name,fact] : judge->snapshotFacts) { (void)fact; m.judgeInitial[name] = judge->initialState(name); }
    for (const auto& [name,macro] : judge->judgeMacros) { (void)macro; m.judgeInitial[name] = judge->initialState(name); }
  }
  if (!judge && c.probe.dialect.cplusplus)
    m.judgeInitial["__cplusplus"] = {MacroState::known,Macro{false,{},false,std::to_string(c.probe.dialect.cplusplus)+"L"},{FactOrigin::commandOperation}};
  auto triple = c.probe.triple;
  for (const auto& a : c.command.args) if (a.option == "-m32" || a.option == "-m64") {
    const auto separator = triple.find('-'); const auto arch = triple.substr(0,separator);
    if (separator == triple.npos || !(arch == "x86_64" || arch == "i386" || arch == "i486" || arch == "i586" || arch == "i686"))
      m.disabledReasons.push_back("unsupported ABI translation: " + a.option);
    else triple = std::string(a.option == "-m32" ? "i386" : "x86_64") + triple.substr(separator);
  }
  // CreateFromArgs takes cc1 options, not driver flags. Reproduce the parsing
  // defaults explicitly where cc1 differs; see the audit in core/README.md.
  // RTTI is already enabled by cc1's C++ language default.
  m.parseArgv = {"-x", "c++", "-std=" + m.dialect, "-triple", triple, "-fsyntax-only",
                 "-fgnuc-version=4.2.1", "-fmath-errno", "-fdeprecated-macro",
                 "-nostdinc++", "-nostdsysteminc", "-nobuiltininc", "-resource-dir", std::string(pruningResourceDirectory)};
  auto add = [&](std::string flag, std::string value) { m.parseArgv.push_back(std::move(flag)); m.parseArgv.push_back(std::move(value)); };
  bool resource = false;
  for (const auto& d : c.probe.searchLists.directories) {
    if (d.gccInternal) {
      if (!resource) add("-isystem",std::string(pruningResourceDirectory) + "/include");
      resource = true; continue;
    }
    add(d.framework ? "-iframework" : d.kind == IncludeKind::quote ? "-iquote" : "-isystem", pathToUtf8(c.command.cwd / d.path));
  }
  if (!resource) add("-isystem",std::string(pruningResourceDirectory) + "/include");
  for (const auto& a : c.command.args) {
    if (a.option == "-D" || a.option == "-U") { m.parseArgv.push_back(a.option + a.value); continue; }
    if (a.option == "-std=" || a.classification == Class::input || a.classification == Class::controlled ||
        a.option == "-I" || a.option == "-iquote" || a.option == "-isystem" || a.option == "-m32" || a.option == "-m64") continue;
    if (a.option == "-funsigned-char") m.parseArgv.push_back("-fno-signed-char");
    else if (a.option == "-fsigned-char") m.parseArgv.push_back("-fsigned-char");
    else if (a.option == "-fshort-wchar" || a.option == "-fno-rtti" || a.option == "-pthread") m.parseArgv.push_back(a.option);
    else if (a.option == "-fno-exceptions") { /* Leave both cc1 exception flags off; set defaults below. */ }
    else {
      std::string spelling; for (const auto& word : a.spelling) { if (!spelling.empty()) spelling += ' '; spelling += word; }
      m.untranslatedOptions.push_back(spelling);
      const bool inert = a.classification == Class::harmless;
      if (!inert) m.disabledReasons.push_back("untranslated semantic option: " + spelling);
    }
  }
  if (std::none_of(c.command.args.begin(),c.command.args.end(),[](const auto& a) { return a.option == "-fno-exceptions"; })) {
    m.parseArgv.push_back("-fcxx-exceptions"); m.parseArgv.push_back("-fexceptions");
  }
  m.parseArgv.push_back("flat.cpp");
  auto s = scanDirectives(m.text); if (!s.ok()) return s.error();
  for (const auto& l : s.value().lines) if (l.directive) {
    const auto& d = *l.directive;
    if (d.name == "line" && l.firstLine <= m.lineMap.size() && !m.lineMap[l.firstLine-1]) m.generatedMarkers.push_back({l.begin,l.end});
    else m.sourceDirectives.push_back({l.begin,l.end});
    if (d.name == "pragma" && !supportedPragma(d)) m.disabledReasons.push_back("unsupported user pragma at line " + std::to_string(l.firstLine) + ": " + l.text);
    if (d.name == "include" && !d.tokens.empty()) {
      const auto& token = d.tokens.front();
      if (token.kind != TokenKind::header) m.disabledReasons.push_back("computed system include at line " + std::to_string(l.firstLine) + ": " + token.text);
      else m.includes.push_back(token.text.substr(1,token.text.size()-2));
    }
  }
  return m;
}
Result<PruningModel> extractPruning(const PruningModel& in, const PruneWorkerOptions& o) {
  auto disabled = [&](std::string reason) -> Result<PruningModel> {
    auto m = in; m.disabledReasons.push_back(std::move(reason)); return m;
  };
  if (o.timeout.count() <= 0 || (o.deadline && *o.deadline <= std::chrono::steady_clock::now())) return disabled("pruning worker deadline exceeded");
  auto encoded = serializePruningModel(in); if (!encoded.ok()) return encoded.error();
  try {
    TemporaryDirectory work(".distill-prune-",o.temporaryRoot.empty() ? std::nullopt : std::optional{o.temporaryRoot});
    const auto input = work.path / "input";
    { std::ofstream f(input,std::ios::binary); f << encoded.value(); f.close(); if (!f) return Error{"cannot write pruning worker input"}; }
    ProcessSpec spec; spec.cwd = work.path; spec.timeout = o.timeout; spec.maxOutputBytes = o.maxOutputBytes;
    spec.argv = {pathToUtf8(o.executable),"--internal-prune",pathToUtf8(input)};
    if (o.deadline) spec.timeout = std::min(spec.timeout,std::chrono::duration_cast<std::chrono::milliseconds>(*o.deadline-std::chrono::steady_clock::now()));
    if (spec.timeout.count() <= 0) return disabled("pruning worker deadline exceeded");
    auto context = [&](const ProcessResult* r) {
      std::ostringstream out;
      out << "; exit_code=";
      if (r) out << r->exitCode; else out << "unavailable";
      out << "; signal=";
      if (r && r->signal) out << *r->signal; else out << (r ? "none" : "unavailable");
      out << "; timed_out=" << (r ? (r->timedOut ? "true" : "false") : "unavailable")
          << "; stdout_truncated=" << (r ? (r->stdoutTruncated ? "true" : "false") : "unavailable")
          << "; stderr_truncated=" << (r ? (r->stderrTruncated ? "true" : "false") : "unavailable")
          << "; timeout_ms=" << spec.timeout.count() << "; max_output_bytes=" << spec.maxOutputBytes
          << "; cwd=" << std::quoted(pathToUtf8(spec.cwd)) << "; argv=[";
      for (std::size_t i = 0; i < spec.argv.size(); ++i) out << (i ? ", " : "") << std::quoted(spec.argv[i]);
      out << "]; stderr (first 2048 bytes):\n";
      if (r) {
        out << r->stderrText.substr(0,2048);
        if (r->stderrText.size() > 2048) out << "\n[stderr excerpt truncated]";
      } else out << "[unavailable]";
      return out.str();
    };
    // Do not impose a small address-space cap on full AST parsing:
    // libstdc++ headers and embedded Clang resources need substantially more AS.
    auto run = o.runner(spec);
    if (!run.ok()) return disabled("pruning worker: " + run.error().message + context(nullptr));
    const auto& r = run.value();
    if (r.timedOut || r.exitCode || r.signal || r.stdoutTruncated || r.stderrTruncated)
      return disabled("pruning worker failed or reached a limit" + context(&r));
    auto model = parsePruningModel(r.stdoutText);
    if (!model.ok()) return disabled("invalid pruning worker result: " + model.error().message + context(&r));
    if (model.value().text != in.text || model.value().parseArgv != in.parseArgv || model.value().lineMap != in.lineMap || model.value().generatedMarkers != in.generatedMarkers ||
        model.value().sourceDirectives != in.sourceDirectives || model.value().includes != in.includes ||
        model.value().keepNames != in.keepNames || model.value().keepOffsets != in.keepOffsets || model.value().dialect != in.dialect ||
        model.value().unknownInUserCode != in.unknownInUserCode ||
        !std::all_of(in.disabledReasons.begin(),in.disabledReasons.end(),[&](const auto& reason) {
          return std::find(model.value().disabledReasons.begin(),model.value().disabledReasons.end(),reason) != model.value().disabledReasons.end();
        }))
      return disabled("pruning worker changed immutable input" + context(&r));
    // Clang can return a valid, disabled model after a parse/configuration
    // error. Keep its diagnostics just as for an unsuccessful worker exit.
    for (auto& reason : model.value().disabledReasons)
      if (reason == "Clang parse error" || reason == "invalid Clang parse configuration" || reason == "unsupported Clang target")
        reason += context(&r);
    return model;
  } catch (const Error& e) { return e; }
  catch (const std::exception& e) { return Error{std::string("pruning worker: ")+e.what()}; }
}
} // namespace distill
