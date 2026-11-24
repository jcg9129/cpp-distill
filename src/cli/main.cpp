// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/command/options.h"
#include "cli/judge.h"
#include "core/pipeline/pipeline.h"
#include "core/path.h"
#include "core/temporary.h"
#include "clang/evaluate.h"
#include "clang/prune_extract.h"
#include <atomic>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
using namespace distill;
template<class T> T take(Result<T> r) { if (!r.ok()) throw r.error(); return std::move(r.value()); }
// Each Clang evaluation lives in a bounded child of this executable. The
// protocol is private and file-based so the D2a runner can contain it on POSIX
// and Windows alike, with no fork of a process that has initialized LLVM.
class WorkerEvaluator final : public ConditionEvaluator {
  std::filesystem::path executable_;
  TemporaryDirectory work_{".distill-worker-"};
public:
  explicit WorkerEvaluator(std::filesystem::path executable) : executable_(std::move(executable)) {}
  Result<bool> evaluate(const ConditionInput& in) override {
    static std::atomic<unsigned> sequence{0};
    struct Input {
      std::filesystem::path file;
      ~Input() { std::error_code ec; std::filesystem::remove(file, ec); }
    } input{work_.path / (".distill-condition-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(sequence++))};
    {
      std::ofstream f(input.file, std::ios::binary);
      const auto m = in.requirements.integers.value_or(IntegerModel{});
      f << std::quoted(in.requirements.dialect.value_or("gnu++20")) << ' ' << in.requirements.integers.has_value() << ' '
        << m.charBits << ' ' << m.intBits << ' ' << m.longBits << ' ' << m.longLongBits << ' ' << m.wcharBits << ' ' << m.intmaxBits << ' ' << m.charSigned << ' ' << m.wcharSigned << '\n'
        << std::quoted(in.trueToken) << ' ' << std::quoted(in.falseToken) << '\n' << in.source;
      f.close(); if (!f) return Error{"cannot write condition worker input"};
    }
    ProcessSpec spec; spec.cwd = work_.path;
    spec.argv = {pathToUtf8(executable_), "--internal-condition", pathToUtf8(input.file)};
    spec.maxOutputBytes = 4096;
    if (in.deadline) {
      spec.timeout = std::min(spec.timeout, std::chrono::duration_cast<std::chrono::milliseconds>(*in.deadline - std::chrono::steady_clock::now()));
      if (spec.timeout.count() <= 0) return Error{"condition deadline exceeded"};
    }
    auto r = runProcess(spec);
    if (!r.ok()) return r.error();
    if (r.value().timedOut || r.value().exitCode || r.value().signal || r.value().stdoutTruncated || r.value().stderrTruncated)
      return Error{"Clang worker failed or reached a limit"};
    if (r.value().stdoutText == "true\n") return true;
    if (r.value().stdoutText == "false\n") return false;
    return Error{"invalid Clang worker result"};
  }
};
int pruneWorker(const std::string& file) {
  std::ifstream f(pathFromUtf8(file),std::ios::binary);
  if (!f) throw Error{"cannot open pruning input: " + file};
  std::string bytes; char buffer[16384];
  while (f.read(buffer,sizeof(buffer)) || f.gcount()) {
    if (bytes.size() + static_cast<std::size_t>(f.gcount()) > 256 * 1024 * 1024)
      throw Error{"pruning input exceeds protocol limit: " + file};
    bytes.append(buffer,static_cast<std::size_t>(f.gcount()));
  }
  if (!f.eof()) throw Error{"cannot read pruning input: " + file};
  auto m = take(extractPruningInWorker(take(parsePruningModel(bytes))));
  std::cout << take(serializePruningModel(m));
  return std::cout ? 0 : 1;
}
int worker(const std::string& file) {
  std::ifstream f(pathFromUtf8(file), std::ios::binary);
  ConditionInput in; IntegerModel m; bool integers = false; std::string dialect;
  if (!(f >> std::quoted(dialect) >> integers >> m.charBits >> m.intBits >> m.longBits >> m.longLongBits >> m.wcharBits >> m.intmaxBits >> m.charSigned >> m.wcharSigned
          >> std::quoted(in.trueToken) >> std::quoted(in.falseToken))) return 1;
  f.get(); in.source.assign(std::istreambuf_iterator<char>(f), {});
  in.requirements.dialect = dialect; if (integers) in.requirements.integers = m;
  ClangEvaluator evaluator;
  auto r = evaluator.evaluate(in);
  if (!r.ok()) return 1;
  std::cout << (r.value() ? "true\n" : "false\n"); return 0;
}
void publish(const std::filesystem::path& file, std::string_view bytes) {
  // A private sibling directory guarantees a same-filesystem rename, without
  // truncating a previous output if writing the checked bytes fails.
  const auto absolute = std::filesystem::absolute(file);
  TemporaryDirectory staging(".distill-publish-",absolute.parent_path());
  const auto temp = staging.path / "output";
  std::ofstream out(temp, std::ios::binary); out << bytes; out.close();
  if (!out) throw Error{"cannot write " + pathToUtf8(file)};
#ifdef _WIN32
  // std::filesystem::rename cannot replace an existing destination on Windows.
  if (!MoveFileExW(temp.c_str(),absolute.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    throw Error{"cannot rename checked output to " + pathToUtf8(file) + ": Windows error " + std::to_string(GetLastError())};
#else
  std::filesystem::rename(temp,absolute);
#endif
}
int run(const std::vector<std::string>& args) {
  if (args.size() == 3 && args[1] == "--internal-prune") return pruneWorker(args[2]);
  if (args.size() == 3 && args[1] == "--internal-condition") return worker(args[2]);
  auto parsed = take(parseDistillOptions({args.begin() + 1, args.end()}));
  if (parsed.help) { std::cout << distillHelp(); return 0; }
  if (parsed.version) { std::cout << distillVersion(); return 0; }
  if (parsed.judgeProbe || parsed.judgeCheck) return runJudgeDiagnostic(parsed);
  std::optional<Profile> profile;
  if (parsed.judge) {
    ProfileLookup lookup;
    for (const char* name : {"HOME", "XDG_CONFIG_HOME", "APPDATA"}) if (const auto* value = std::getenv(name)) lookup.environment[name] = value;
    if (*parsed.judge == "list") {
      auto catalog = take(listProfiles(lookup));
      for (const auto& warning : catalog.warnings) std::cerr << warning << '\n';
      std::cout << renderProfileList(catalog);
      return std::cout ? 0 : 1;
    }
    profile = take(lookupProfile(*parsed.judge, lookup));
    for (const auto& warning : profile->warnings) std::cerr << warning << '\n';
    std::cerr << profileSelectionLine(*profile);
    std::cerr << profileStalenessNote(*profile);
  }
  const auto cwd = std::filesystem::current_path();
  const auto command = take(parseCompileCommand(parsed.compile, cwd));
  // Prevent output/report aliases from overwriting input or one another.
  auto same = [&](const std::string& a, const std::filesystem::path& b) {
    std::error_code ec;
    if (std::filesystem::equivalent(cwd / pathFromUtf8(a), cwd / b, ec) && !ec) return true;
    return std::filesystem::weakly_canonical(cwd / pathFromUtf8(a)) == std::filesystem::weakly_canonical(cwd / b);
  };
  if ((parsed.output && same(*parsed.output, command.source)) || (parsed.report && same(*parsed.report, command.source)) ||
      (parsed.output && parsed.report && same(*parsed.output, pathFromUtf8(*parsed.report)))) throw Error{"output/report must differ from the input and one another"};
  DistillOptions options; options.profile = profile ? &*profile : nullptr;
  options.compilerExplicit = parsed.compilerExplicit; options.strict = parsed.strict;
  options.noPrune = parsed.noPrune; options.stripComments = parsed.stripComments; options.keep = parsed.keep;
  options.minify = parsed.minify;
  options.assumeLibrarySpelledNames = parsed.assumeLibrarySpelledNames;
  options.assumeInstantiationEffects = parsed.assumeInstantiationEffects;
  options.assumeLibrarySpelledMembers = parsed.assumeLibrarySpelledMembers;
  options.assumeLibraryMacroValues = parsed.assumeLibraryMacroValues;
  options.assumeUnspelledFunctions = parsed.assumeUnspelledFunctions;
  options.assumeIdempotentStandardHeaders = parsed.assumeIdempotentStandardHeaders;
  options.assumeUnobservedGuardMacros = parsed.assumeUnobservedGuardMacros;
  options.noLineMarkers = parsed.noLineMarkers;
  options.compactWhitespace = parsed.compactWhitespace;
  options.samples = take(collectRunSamples(parsed.verifyRun,cwd));
  if (parsed.verbose) options.progress = [](std::string_view step) { std::cerr << "distill: " << step << '\n'; };
  auto executable = runningExecutable();
  if (!executable.ok()) executable = resolveExecutable(pathFromUtf8(args[0]), cwd);
  options.pruningWorker = take(std::move(executable));
  WorkerEvaluator evaluator(options.pruningWorker);
  auto result = take(distillProgram(command, options, evaluator));
  if (parsed.report) publish(pathFromUtf8(*parsed.report), result.report);
  std::cerr << result.diagnostic;
  if (result.exitCode != 1) {
    if (parsed.output) publish(pathFromUtf8(*parsed.output), result.bytes);
    else { std::cout << result.bytes; std::cout.flush(); if (!std::cout) throw Error{"cannot write stdout"}; }
  }
  std::cerr << result.resultLine << '\n'; return result.exitCode;
}
int entry(const std::vector<std::string>& args) {
  try { return run(args); }
  catch (const Error& e) { std::cerr << "distill: " << e.message << '\n'; return 1; }
  catch (const std::exception& e) { std::cerr << "distill: " << e.what() << '\n'; return 1; }
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
  _setmode(_fileno(stdout), _O_BINARY); _setmode(_fileno(stderr), _O_BINARY);
  std::vector<std::string> args;
  for (int i = 0; i < argc; ++i) args.push_back(distill::pathToUtf8(std::filesystem::path(argv[i])));
  return entry(args);
}
#else
int main(int argc, char** argv) { return entry({argv, argv + argc}); }
#endif
