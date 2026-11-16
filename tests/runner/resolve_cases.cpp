// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/inline/inline.h"
#include "core/resolve/resolve.h"
#include "evaluate.h"
#include "core/lex/scan.h"
#include "core/path.h"
#include "core/process/process.h"
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <set>
#include <stdexcept>
#include <tuple>

namespace {
using namespace distill;
std::string read(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("cannot read " + pathToUtf8(path));
  return {std::istreambuf_iterator<char>(file), {}};
}
void write(const std::filesystem::path& path, std::string_view text) {
  std::ofstream file(path, std::ios::binary); file << text;
  if (!file) throw std::runtime_error("cannot write " + pathToUtf8(path));
}
template<class T> T require(Result<T> r) {
  if (!r.ok()) throw std::runtime_error(r.error().message);
  return std::move(r.value());
}
void check(bool condition, std::string message) { if (!condition) throw std::runtime_error(std::move(message)); }
std::string uncomment(std::string text) {
  std::istringstream in(text); std::string line, out;
  while (std::getline(in, line)) if (!line.starts_with('#')) { if (!out.empty()) out += '\n'; out += line; }
  while (!out.empty() && (out.back() == '\r' || out.back() == '\n')) out.pop_back();
  return out;
}
// cmd.txt uses shell-style whitespace/quotes/backslashes, without shell expansion.
std::vector<std::string> words(std::string_view text) {
  std::vector<std::string> result; std::string word; char quote = 0; bool started = false;
  for (std::size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (c == '\\' && quote != '\'') { check(i + 1 < text.size(), "dangling command escape"); word += text[++i]; started = true; }
    else if (quote) { if (c == quote) quote = 0; else word += c; }
    else if (c == '\'' || c == '"') { quote = c; started = true; }
    else if (c == ' ' || c == '\n' || c == '\r' || c == '\t') { if (started) { result.push_back(word); word.clear(); started = false; } }
    else { word += c; started = true; }
  }
  check(!quote, "unclosed command quote"); if (started) result.push_back(word); return result;
}
struct Temporary {
  std::filesystem::path path;
  Temporary(const std::filesystem::path& root) {
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
      auto p = root / (".distill-cases-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(attempt));
      if (std::filesystem::create_directory(p)) { path = p; return; }
    }
    throw std::runtime_error("cannot create case temporary directory");
  }
  ~Temporary() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};
ProcessResult process(std::vector<std::string> argv, const std::filesystem::path& cwd) {
  ProcessSpec spec; spec.argv = std::move(argv); spec.cwd = cwd; spec.maxOutputBytes = 32 * 1024 * 1024;
  auto r = require(runProcess(spec));
  check(!r.timedOut && !r.stdoutTruncated && !r.stderrTruncated, "process timeout/output cap");
  return r;
}
// GCC linemarkers assign the number to the NEXT line. Flags (1/2/3/4) do
// not advance it. Blank lines do advance it, including blank marker substitutes.
bool marker(std::string_view line, SourceLocation& location) {
  if (!line.starts_with('#')) return false;
  line.remove_prefix(1);
  while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
  std::size_t number = 0;
  auto parsed = std::from_chars(line.data(), line.data() + line.size(), number);
  if (parsed.ec != std::errc{}) return false;
  line.remove_prefix(static_cast<std::size_t>(parsed.ptr - line.data()));
  while (!line.empty() && line.front() == ' ') line.remove_prefix(1);
  if (line.empty() || line.front() != '"') return false;
  std::string file;
  bool closed = false;
  for (std::size_t i = 1; i < line.size(); ++i) {
    char c = line[i];
    if (c == '"') { closed = true; break; }
    if (c == '\\' && i + 1 < line.size()) c = line[++i];
    file += c;
  }
  check(closed, "malformed GCC marker"); location = {file, number}; return true;
}
using LocatedToken = std::tuple<std::string, std::string, std::size_t>;
std::vector<LocatedToken> normalize(std::string_view output) {
  // Marker-looking lines inside raw literals are ordinary token contents.
  const auto originalScan = require(scanDirectives(output));
  std::set<std::size_t> directiveOffsets;
  for (const auto& line : originalScan.lines) if (line.directive) directiveOffsets.insert(line.directive->begin);
  std::string content;
  std::vector<SourceLocation> lines;
  SourceLocation location{"<initial>", 1};
  for (std::size_t begin = 0; begin < output.size();) {
    const auto end = output.find('\n', begin);
    const auto len = end == output.npos ? output.size() - begin : end - begin;
    auto line = output.substr(begin, len);
    if (directiveOffsets.contains(begin) && marker(line, location)) { content += '\n'; lines.push_back({"<marker>", 0}); }
    else { content.append(line); content += '\n'; lines.push_back(location); ++location.line; }
    begin += len + (end == output.npos ? 0 : 1);
  }
  const auto scan = require(scanDirectives(content));
  std::vector<LocatedToken> result;
  for (const auto& line : scan.lines) for (const auto& token : line.tokens) {
    const auto& origin = lines.at(token.line - 1);
    result.emplace_back(token.text, origin.file, origin.line);
  }
  return result;
}
void compareTokens(const std::string& a, const std::string& b) {
  const auto left = normalize(a), right = normalize(b);
  std::size_t i = 0;
  for (; i < left.size() && i < right.size(); ++i) if (left[i] != right[i]) break;
  if (i == left.size() && i == right.size()) return;
  auto show = [&](const auto& tokens) {
    if (i == tokens.size()) return std::string("<end>");
    const auto& [text, file, line] = tokens[i]; return text + " at " + file + ":" + std::to_string(line);
  };
  throw std::runtime_error("token " + std::to_string(i) + " differs: original " + show(left) + "; raw " + show(right));
}
void checkMap(const InlineResult& result) {
  const auto count = std::count(result.text.begin(), result.text.end(), '\n') +
      (!result.text.empty() && result.text.back() != '\n' ? 1 : 0);
  check(result.lineMap.size() == static_cast<std::size_t>(count), "line map size differs");
  SourceLocation current{"<initial>", 1};
  std::istringstream lines(result.text); std::string line; std::size_t i = 0;
  while (std::getline(lines, line)) {
    if (!result.lineMap.at(i) && line.starts_with("#line ")) {
      check(!result.lineMap.at(i), "generated marker has a source location");
      check(marker("# " + line.substr(6), current), "cannot parse generated marker");
    } else {
      if (result.lineMap.at(i)) check(*result.lineMap[i] == current, "line map disagrees with markers at output line " + std::to_string(i + 1));
      ++current.line;
    }
    ++i;
  }
}
bool hasMain(const std::string& bytes) {
  const auto scan = require(scanDirectives(bytes));
  return std::any_of(scan.identifiers.begin(), scan.identifiers.end(), [](const auto& t) { return t.text == "main"; });
}
void normalizationSelfTest() {
  const auto raw = normalize("# 1 \"a\"\nR\"(\n# 99 \"fake\"\n)\";\nint x;\n");
  check(std::get<0>(raw.at(0)).find("# 99 \"fake\"") != std::string::npos, "normalizer keeps raw literal contents");
  check(normalize("# 3 \"a\\\\b\\\"c\" 1 3\n\nint x;\n") == normalize("# 4 \"a\\\\b\\\"c\"\nint x;\n"), "normalizer blank lines/escapes/flags");
  check(normalize("# 1 \"a\"\nx ++ y\n") != normalize("# 1 \"a\"\nx + + y\n"), "normalizer token boundaries");
  check(normalize("# 1 \"a\"\nx\n") != normalize("# 2 \"a\"\nx\n"), "normalizer detects wrong lines");
  check(normalize("# 1 \"a\"\nx\n") != normalize("# 1 \"b\"\nx\n"), "normalizer detects wrong filenames");
}

Profile localProfile(const ProbeResult& probe, const std::filesystem::path& cwd,
                     std::vector<std::string> flags = {"-O0", "-fsigned-char", "-DDISTILL_FIXTURE_JUDGE=1"}) {
  Profile p;
  p.name = "local-fixture"; p.compiler = probe.identity; p.dialect = probe.dialect;
  p.triple = probe.triple; p.reproduced = true;
  p.libraries = {probe.triple.find("mingw") != std::string::npos ? "mingw-w64" : probe.triple.find("darwin") != std::string::npos ? "macos-libc" : "glibc", "libstdc++"};
  p.flags = std::move(flags);
  auto args = p.flags; args.insert(args.begin(), "g++"); args.push_back("profile.cpp");
  p.macroOperations = require(parseCompileCommand(args, cwd)).localMacros();
  for (const auto& [name, macro] : probe.macros)
    p.snapshotFacts[name] = {MacroState::known, macro, {FactOrigin::compilerBuiltin}};
  // Probe special bindings independently, under the same fixture command.
  auto builtins = specialBuiltins(probe.identity);
  for (const auto& [file, text] : embeddedProfileFiles()) if (file.starts_with("builtins/")) {
    std::istringstream lines(text); std::string line;
    while (std::getline(lines, line)) if (!line.empty() && !line.starts_with('#')) builtins.insert(line);
  }
  for (const char* name : {"__has_builtin", "__has_attribute", "__has_cpp_attribute", "__has_feature"}) builtins.insert(name);
  builtins.insert("__has_include"); builtins.insert("__LINE__"); builtins.insert("__FILE__");
  std::string source;
  const auto file = cwd / "special.cpp";
  std::vector<std::string> ordered(builtins.begin(), builtins.end());
  for (std::size_t i = 0; i < ordered.size(); ++i)
    source += "#ifdef " + ordered[i] + "\nDISTILL_PRESENT_" + std::to_string(i) + "\n#endif\n";
  write(file, source);
  const auto compiler = require(resolveExecutable(pathFromUtf8(std::getenv("DISTILL_TEST_GXX") ? std::getenv("DISTILL_TEST_GXX") : "g++"), cwd));
  std::vector<std::string> specialArgs{pathToUtf8(compiler), "-E", "-P", "-x", "c++", "-std=" + p.dialect.name};
  specialArgs.insert(specialArgs.end(), p.flags.begin(), p.flags.end()); specialArgs.push_back(pathToUtf8(file));
  auto result = process(specialArgs, cwd);
  check(result.exitCode == 0, "special builtin probe failed");
  const auto present = require(scanDirectives(result.stdoutText)); std::set<std::string> active;
  for (const auto& t : present.identifiers) active.insert(t.text);
  for (std::size_t i = 0; i < ordered.size(); ++i) if (!p.snapshotFacts.contains(ordered[i])) {
    const bool defined = active.contains("DISTILL_PRESENT_" + std::to_string(i));
    p.snapshotFacts[ordered[i]] = {defined ? MacroState::definedUnknown : MacroState::undefined, {}, {FactOrigin::specialBuiltin}};
  }
  write(file, "");
  auto startupArgs = specialArgs;
  startupArgs.erase(std::find(startupArgs.begin(), startupArgs.end(), "-P"));
  startupArgs.insert(startupArgs.begin() + 1, "-dD");
  const auto startup = process(startupArgs, cwd);
  check(startup.exitCode == 0, "startup header probe failed");
  std::istringstream startupLines(startup.stdoutText); std::string startupLine;
  SourceLocation startupAt{"<initial>", 1};
  while (std::getline(startupLines, startupLine)) {
    if (marker(startupLine, startupAt)) continue;
    if (startupLine.starts_with("#define ") && startupAt.file.ends_with("stdc-predef.h")) {
      const auto macros = require(parseMacros(startupLine + '\n'));
      for (const auto& [name, macro] : macros) p.snapshotFacts[name] = {MacroState::known, macro, {FactOrigin::startupHeader}};
    }
  }
  std::map<std::string, LocalMacro> finalOperations;
  for (const auto& op : p.macroOperations) finalOperations[op.value.substr(0, op.value.find_first_of("(="))] = op;
  for (const auto& [operationName, op] : finalOperations) {
    (void)operationName;
    const auto name = op.value.substr(0, op.value.find_first_of("(="));
    auto& fact = p.snapshotFacts[name];
    if (std::find(fact.provenance.begin(), fact.provenance.end(), FactOrigin::startupHeader) != fact.provenance.end()) continue;
    if (op.option == "-U") fact = {MacroState::undefined, {}, {FactOrigin::commandOperation}};
    else fact.provenance = {FactOrigin::commandOperation};
  }
  // This runtime snapshot comes from the actual compiler; it is never substituted
  // with shipped judge facts for an equivalence claim.
  std::string snapshot = "# SPDX-License-Identifier: GPL-3.0-or-later\n";
  for (const auto& [name, macro] : probe.macros) {
    snapshot += "#define " + name;
    if (macro.functionLike) {
      snapshot += '(';
      for (std::size_t i = 0; i < macro.parameters.size(); ++i) { if (i) snapshot += ','; snapshot += macro.parameters[i]; }
      snapshot += ')';
    }
    snapshot += ' ' + macro.replacement + '\n';
  }
  write(cwd / "local-predefined.txt", snapshot);
  return p;
}
std::string decisionName(const ResolutionDecision& d) {
  if (d.decision == Decision::trueValue) return "true";
  if (d.decision == Decision::falseValue) return "false";
  if (d.decision == Decision::dead) return "dead";
  return "unknown:" + d.reason;
}
int run(int argc, char** argv) {
  normalizationSelfTest();
  const std::filesystem::path root = DISTILL_RESOLVE_CASES;
  const auto work = std::filesystem::current_path(); Temporary temporary(work);
  const auto gcc = testing::testGcc();
  auto evaluator = testing::testEvaluator(temporary.path);
  if (!evaluator) { std::cout << "SKIP resolver cases: GCC unavailable\n"; return 0; }
  const std::string compiler = gcc ? pathToUtf8(*gcc) : "g++";
  auto fixtureCommand = require(parseCompileCommand({compiler, "main.cpp", "-std=gnu++20", "-fsigned-char", "-O0", "-DDISTILL_FIXTURE_JUDGE=1"}, temporary.path));
  ProbeOptions po; po.temporaryRoot = temporary.path;
  std::optional<ProbeResult> probe;
  std::optional<Profile> local;
  if (gcc) {
    probe = require(probeCompiler(*gcc, fixtureCommand, po));
    local = localProfile(*probe, temporary.path);
  }
  std::vector<std::filesystem::path> cases;
  for (const auto& e : std::filesystem::directory_iterator(root)) if (e.is_directory()) cases.push_back(e.path());
  std::sort(cases.begin(), cases.end());
  int passed = 0, failed = 0, compared = 0, ran = 0, decisions = 0;
  for (const auto& dir : cases) {
    const auto name = pathToUtf8(dir.filename());
    if (argc == 3 && std::string_view(argv[1]) == "--filter" && name.find(argv[2]) == name.npos) continue;
    if (name == "win32_main" && probe && probe->triple.find("linux") == std::string::npos) {
      std::cout << "SKIP win32_main: requires a Linux compiler\n"; continue;
    }
    try {
      const auto original = temporary.path / name; std::filesystem::create_directory(original);
      for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
        const auto destination = original / entry.path().lexically_relative(dir);
        if (entry.is_directory()) std::filesystem::create_directories(destination);
        else write(destination, read(entry.path()));
      }
      auto args = words(uncomment(read(original / "cmd.txt"))); check(!args.empty(), "empty cmd.txt"); args[0] = compiler;
      const auto command = require(parseCompileCommand(args, original));
      SearchLists lists = probe ? probe->searchLists : SearchLists{};
      const auto inlined = require(inlineSources(command, require(buildIncludeSearch(command, lists)))); checkMap(inlined);
      ResolveOptions options; std::optional<Profile> selected;
      std::string profileName;
      if (std::filesystem::exists(original / "profile-gcc15-gnupp20.toml")) { selected = require(loadProfile(original / "profile-gcc15-gnupp20.toml")); profileName = "file"; }
      else if (std::filesystem::exists(original / "profile.txt")) {
        profileName = uncomment(read(original / "profile.txt"));
        if (profileName == "local") { if (!local) throw std::runtime_error("local fixture needs GCC"); selected = *local; }
        else selected = require(lookupProfile(profileName));
      }
      if (selected) options.profile = &*selected;
      auto result = require(resolveConditions(inlined, command, *evaluator, options));
      InlineResult flat; flat.text = result.text; flat.lineMap = result.lineMap; checkMap(flat);
      if (std::filesystem::exists(original / "expect-disabled.txt")) {
        const auto expected = uncomment(read(original / "expect-disabled.txt"));
        check(!result.disabledReasons.empty(), "expected disabled resolution");
        check(std::any_of(result.disabledReasons.begin(), result.disabledReasons.end(), [&](const auto& r) { return r.find(expected) != r.npos; }), "wrong disable reason");
        check(result.text == inlined.text && result.lineMap == inlined.lineMap, "disabled resolver changed raw");
      } else check(result.disabledReasons.empty(), "unexpected disabled resolution");
      std::vector<const ResolutionDecision*> conditions;
      for (const auto& d : result.decisions) if (d.original && d.directive != "else" && d.directive != "endif") conditions.push_back(&d);
      auto expectedText = uncomment(read(original / "expect.txt")); std::istringstream expected(expectedText); std::string line; std::size_t index = 0;
      while (std::getline(expected, line)) {
        if (line.empty()) continue;
        check(index < conditions.size(), "too many expected decisions");
        const auto& d = *conditions[index++];
        const auto space = line.find(' '); check(space != line.npos, "invalid expected decision");
        const auto location = d.original->file + ":" + std::to_string(d.original->line);
        check(line.substr(0, space) == location, "decision location differs: " + line + " vs " + location);
        auto actual = decisionName(d), wanted = line.substr(space + 1);
        check(actual == wanted || (wanted == "unknown" && d.decision == Decision::unknown), "decision differs at " + location + ": wanted " + wanted + ", got " + actual);
        ++decisions;
      }
      check(index == conditions.size(), "missing expected decisions");
      // Only shipped profiles use a different compiler and skip equivalence.
      if (gcc && (profileName == "local" || profileName.empty() || profileName == "file")) {
        std::optional<Profile> actualProfile = local;
        if (profileName == "file") {
          auto flags = std::vector<std::string>{};
          for (const auto& arg : command.args) {
            if (arg.classification == Class::input || arg.classification == Class::controlled || arg.option == "-D" || arg.option == "-U") continue;
            flags.insert(flags.end(), arg.spelling.begin(), arg.spelling.end());
          }
          flags.push_back("-std=" + selected->dialect.name);
          flags.insert(flags.end(), selected->flags.begin(), selected->flags.end());
          const auto empty = original / "fixture-empty.cpp"; write(empty, "");
          std::vector<std::string> capture{compiler, "-x", "c++", "-dM", "-E"};
          capture.insert(capture.end(), flags.begin(), flags.end()); capture.push_back(pathToUtf8(empty));
          const auto snapshot = process(capture, original); check(snapshot.exitCode == 0, "fixture snapshot failed");
          auto actualProbe = *probe; actualProbe.macros = require(parseMacros(snapshot.stdoutText)); actualProbe.dialect = selected->dialect;
          actualProfile = localProfile(actualProbe, original, flags);
        }
        if (result.disabledReasons.empty()) {
          ResolveOptions actual; actual.profile = &*actualProfile;
          const auto rerun = require(resolveConditions(inlined, command, *evaluator, actual));
          check(sameDecisions(result, rerun), "local configuration does not satisfy resolution");
        }
        const auto generated = temporary.path / (name + "-generated"); std::filesystem::create_directory(generated);
        const auto rawFile = generated / "raw.cpp", flatFile = generated / "flat.cpp";
        write(rawFile, inlined.text); write(flatFile, result.text);
        // Replace local -D/-U with the fixture profile's ordered operations.
        auto build = [&](const auto& file, const auto& output) {
          auto argv = command.buildArgv(file, output, false, {});
          if (selected) {
            argv.push_back("-std=" + selected->dialect.name);
            for (const auto& flag : selected->flags) argv.push_back(flag);
          }
          return argv;
        };
        auto a = build(rawFile, generated / "raw.exe"), b = build(flatFile, generated / "flat.exe");
        auto pp = [&](auto argv) {
          argv.push_back("-E");
          for (std::size_t i = 0; i + 1 < argv.size(); ++i) if (argv[i] == "-o") argv[++i] = "-";
          auto r = process(argv, original); check(r.exitCode == 0, "preprocess failed: " + r.stderrText); return r.stdoutText;
        };
        const auto ppA = pp(a), ppB = pp(b);
        const bool expectedMismatch = std::filesystem::exists(original / "expect-mismatch.txt");
        bool equal = true;
        try { compareTokens(ppA, ppB); } catch (const std::runtime_error&) { equal = false; if (!expectedMismatch) throw; }
        check(equal != expectedMismatch, "expected preprocess mismatch was not detected"); ++compared;
        if (hasMain(ppA)) {
          auto ca = process(a, original), cb = process(b, original);
          check(ca.exitCode == 0 && cb.exitCode == 0, "build failed: " + ca.stderrText + cb.stderrText);
          auto ra = process({pathToUtf8(generated / "raw.exe")}, original), rb = process({pathToUtf8(generated / "flat.exe")}, original);
          const bool sameRun = ra.stdoutText == rb.stdoutText && ra.exitCode == rb.exitCode && ra.signal == rb.signal;
          check(sameRun != expectedMismatch, expectedMismatch ? "expected runtime mismatch was not detected" : "runtime differs"); ++ran;
        }
      }
      ++passed; std::cout << "PASS " << name << '\n';
    } catch (const std::exception& e) { ++failed; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
  }
  std::cout << passed << " passed, " << failed << " failed; " << decisions << " decisions, " << compared << " token/location comparisons, " << ran << " runtime comparisons\n";
  return failed ? 1 : 0;
}
}
int main(int argc, char** argv) {
  try { return run(argc, argv); }
  catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
