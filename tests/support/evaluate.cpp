// SPDX-License-Identifier: GPL-3.0-or-later
#include "evaluate.h"
#include "core/process/process.h"
#include "core/path.h"
#include "core/lex/scan.h"
#ifdef DISTILL_HAS_CLANG
#include "clang/evaluate.h"
#endif
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace distill::testing {
Result<bool> GccEvaluator::evaluate(const ConditionInput& in) {
  try {
    if (in.requirements.integers) {
      auto target = evaluationTriple(*in.requirements.integers);
      if (!target.ok()) return target.error();
      if (!localModel_) {
        ProcessSpec probe; probe.cwd = root_;
        probe.argv = {pathToUtf8(compiler_), "-dM", "-E", "-x", "c++", "-"};
        auto run = runProcess(probe);
        if (!run.ok()) return run.error();
        if (run.value().exitCode || run.value().timedOut || run.value().stdoutTruncated) return Error{"GCC model probe failed"};
        auto macros = parseMacros(run.value().stdoutText);
        if (!macros.ok()) return macros.error();
        Profile local; local.reproduced = true;
        for (const auto& [name, macro] : macros.value()) local.snapshotFacts[name] = {MacroState::known, macro, {FactOrigin::compilerBuiltin}};
        localModel_ = profileRequirements(local).integers;
      }
      if (!localModel_) return Error{"unknown local GCC integer model"};
      auto requested = *in.requirements.integers, actual = *localModel_;
      actual.charSigned = requested.charSigned; // supplied explicitly below
      if (requested.wcharBits == 16 && !requested.wcharSigned) { actual.wcharBits = 16; actual.wcharSigned = false; }
      if (actual != requested) return Error{"GCC oracle cannot represent requested integer model"};
    }
    static std::atomic<unsigned> sequence{0};
    struct File {
      std::filesystem::path path;
      ~File() { std::error_code ec; std::filesystem::remove(path, ec); }
    } file{root_ / ("condition-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(sequence++) + ".cpp")};
    {
      std::ofstream out(file.path, std::ios::binary);
      out << isolatedConditionSource(in);
      if (!out) return Error{"cannot write evaluator input"};
    }
    ProcessSpec spec;
    spec.cwd = root_;
    spec.argv = {pathToUtf8(compiler_), "-undef", "-E", "-P", "-nostdinc", "-nostdinc++", "-x", "c++",
                 "-std=" + in.requirements.dialect.value_or("gnu++20")};
    if (in.requirements.integers) {
      const auto& m = *in.requirements.integers;
      spec.argv.push_back(m.charSigned ? "-fsigned-char" : "-funsigned-char");
      // Local GCC supports these wchar layouts without target libraries.
      spec.argv.push_back(m.wcharBits == 16 ? "-fshort-wchar" : "-fno-short-wchar");
    }
    spec.argv.push_back(pathToUtf8(file.path));
    auto run = runProcess(spec);
    if (!run.ok()) return run.error();
    const auto& r = run.value();
    if (r.timedOut || r.stdoutTruncated || r.stderrTruncated || r.exitCode || r.signal) return Error{"GCC condition failed: " + r.stderrText};
    auto output = scanDirectives(r.stdoutText);
    if (!output.ok()) return output.error();
    bool yes = false, no = false;
    for (const auto& token : output.value().identifiers) {
      yes |= token.text == in.trueToken; no |= token.text == in.falseToken;
    }
    if (yes == no) return Error{"invalid GCC condition result"};
    return yes;
  } catch (const std::exception& e) { return Error{e.what()}; }
}
std::optional<std::filesystem::path> testGcc() {
  auto root = std::filesystem::current_path();
  auto compiler = resolveExecutable(pathFromUtf8(std::getenv("DISTILL_TEST_GXX") ? std::getenv("DISTILL_TEST_GXX") : "g++"), root);
  if (!compiler.ok()) return {};
  ProcessSpec spec; spec.cwd = root; spec.argv = {pathToUtf8(compiler.value()), "--version"};
  auto r = runProcess(spec);
  if (!r.ok() || r.value().exitCode || r.value().stdoutText.find("clang") != std::string::npos) return {};
  return compiler.value();
}
namespace {
#ifdef DISTILL_HAS_CLANG
class Differential final : public ConditionEvaluator {
  GccEvaluator gcc_;
  ClangEvaluator clang_;
public:
  Differential(std::filesystem::path gcc, std::filesystem::path root) : gcc_(std::move(gcc), std::move(root)) {}
  Result<bool> evaluate(const ConditionInput& in) override {
    auto a = gcc_.evaluate(in), b = clang_.evaluate(in);
    if (a.ok() != b.ok() || (a.ok() && a.value() != b.value()))
      throw std::runtime_error("GCC/Clang differential mismatch on:\n" + in.source + "\nGCC: " + (a.ok() ? std::to_string(a.value()) : a.error().message) + "\nClang: " + (b.ok() ? std::to_string(b.value()) : b.error().message));
    return b;
  }
};
#endif
}
std::unique_ptr<ConditionEvaluator> testEvaluator(const std::filesystem::path& root) {
  const std::string mode = std::getenv("DISTILL_TEST_EVALUATOR") ? std::getenv("DISTILL_TEST_EVALUATOR") : "gcc";
#ifdef DISTILL_HAS_CLANG
  if (mode == "clang") return std::make_unique<ClangEvaluator>();
  if (mode == "differential") {
    const auto gcc = testGcc();
    if (!gcc) throw std::runtime_error("differential test requires GCC");
    return std::make_unique<Differential>(*gcc, root);
  }
#endif
  if (mode != "gcc") throw std::runtime_error("evaluator unavailable: " + mode);
  const auto gcc = testGcc();
  if (!gcc) return {};
  return std::make_unique<GccEvaluator>(*gcc, root);
}
} // namespace distill::testing
