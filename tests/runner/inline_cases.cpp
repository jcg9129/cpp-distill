// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/inline/inline.h"
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
int run(int argc, char** argv) {
  normalizationSelfTest();
  const std::filesystem::path root = DISTILL_INLINE_CASES;
  const auto work = std::filesystem::current_path();
  Temporary temporary(work);
  std::string compiler = std::getenv("DISTILL_TEST_GXX") ? std::getenv("DISTILL_TEST_GXX") : "g++";
  auto executable = resolveExecutable(pathFromUtf8(compiler), work);
  bool gcc = executable.ok();
  if (gcc) {
    auto version = process({pathToUtf8(executable.value()), "--version"}, work);
    gcc = version.exitCode == 0 && version.stdoutText.find("clang") == std::string::npos;
  }
  if (!gcc) std::cout << "SKIP g++ equivalence/build/run checks: GCC unavailable (set DISTILL_TEST_GXX)\n";
  std::vector<std::filesystem::path> cases;
  for (const auto& entry : std::filesystem::directory_iterator(root)) if (entry.is_directory()) cases.push_back(entry.path());
  std::sort(cases.begin(), cases.end());
  int passed = 0, failed = 0, compared = 0, ran = 0;
  for (const auto& dir : cases) {
    const auto name = pathToUtf8(dir.filename());
    if (argc == 3 && std::string_view(argv[1]) == "--filter" && name.find(argv[2]) == name.npos) continue;
    try {
      // Copy byte-exact inputs; fixture setup and all compiler outputs stay away
      // from checked-in cases. raw.cpp is in a sibling, without user include paths.
      const auto original = temporary.path / name; std::filesystem::create_directory(original);
      // Stream copies: some overlay filesystems reject kernel-assisted copy_file.
      for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
        const auto destination = original / entry.path().lexically_relative(dir);
        if (entry.is_directory()) std::filesystem::create_directories(destination);
        else write(destination, read(entry.path()));
      }
      if (name == "once_alias") {
#ifndef _WIN32
        std::filesystem::create_symlink("once.h", original / "alias.h");
#else
        // Dot spelling still exercises identical identity on Windows.
        write(original / "main.cpp", "// SPDX-License-Identifier: GPL-3.0-or-later\n#include \"once.h\"\n#include \"./once.h\"\nint main(){return value != 42;}\n");
#endif
      }
      auto args = words(uncomment(read(original / "cmd.txt")));
      check(!args.empty(), "empty cmd.txt"); if (gcc) args[0] = pathToUtf8(executable.value());
      const auto command = require(parseCompileCommand(args, original));
      SearchLists lists;
      if (gcc) {
        ProbeOptions probeOptions; probeOptions.temporaryRoot = temporary.path;
        lists = require(probeCompiler(command.compiler, command, probeOptions)).searchLists;
      }
      const auto search = require(buildIncludeSearch(command, lists));
      // The collision identifier must be generated after fixture copies establish identities.
      if (name == "once_collision") {
        const auto guard = onceGuardName(require(fileIdentity(original / "once.h")));
        write(original / "collision.h", "// SPDX-License-Identifier: GPL-3.0-or-later\nint " + guard + ";\n");
      }
      auto inlined = inlineSources(command, search);
      if (std::filesystem::exists(original / "expect-error.txt")) {
        const auto expected = uncomment(read(original / "expect-error.txt"));
        check(!inlined.ok(), "expected error: " + expected);
        check(inlined.error().message.find(expected) != std::string::npos, "unexpected error: " + inlined.error().message);
      } else {
        auto result = require(std::move(inlined)); checkMap(result);
        if (name == "pb_ds_parent_path") {
          check(result.systemIncludes.size() == 1, "missing parent-path include metadata");
          check(!result.systemIncludes[0].standard, "parent-path header classified as standard");
        }
        if (name == "counter") check(result.flags.counter, "missing counter flag");
        if (name == "system_quote" && gcc) check(result.systemIncludes.at(0).rewritten, "system quote not rewritten");
        if (name == "once_collision") {
          const auto guard = onceGuardName(require(fileIdentity(original / "once.h")));
          check(result.text.find("#ifndef " + guard + "\n") == std::string::npos, "guard collision not rehashed");
        }
        if (gcc) {
          const auto generated = temporary.path / (name + "-generated"); std::filesystem::create_directory(generated);
          const auto raw = generated / "raw.cpp"; write(raw, result.text);
          auto baseline = command.buildArgv(command.source, generated / "original.exe", true, {}, false);
          auto transformed = command.buildArgv(raw, generated / "raw.exe", true, {});
          auto ppA = baseline, ppB = transformed; ppA.push_back("-E"); ppB.push_back("-E");
          // buildArgv includes -o. Replace that value with '-' for preprocessor stdout.
          for (auto* argsOut : {&ppA, &ppB}) for (std::size_t i = 0; i + 1 < argsOut->size(); ++i) if ((*argsOut)[i] == "-o") (*argsOut)[++i] = "-";
          const auto a = process(ppA, original), b = process(ppB, original);
          check(a.exitCode == 0, "original preprocessing failed: " + a.stderrText);
          check(b.exitCode == 0, "raw preprocessing failed: " + b.stderrText);
          write(generated / "original.ii", a.stdoutText); write(generated / "raw.ii", b.stdoutText);
          compareTokens(a.stdoutText, b.stdoutText); ++compared;
          if (hasMain(a.stdoutText)) {
            auto ca = process(baseline, original), cb = process(transformed, original);
            check(ca.exitCode == 0, "original build failed: " + ca.stderrText); check(cb.exitCode == 0, "raw build failed: " + cb.stderrText);
            auto ra = process({pathToUtf8(generated / "original.exe")}, original), rb = process({pathToUtf8(generated / "raw.exe")}, original);
            check(ra.exitCode == rb.exitCode && ra.signal == rb.signal && ra.stdoutText == rb.stdoutText, "runtime stdout/exit differ"); ++ran;
          }
        }
      }
      ++passed; std::cout << "PASS " << name << '\n';
    } catch (const std::exception& e) { ++failed; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
  }
  std::cout << passed << " passed, " << failed << " failed; " << compared << " GCC token/location comparisons, " << ran << " runtime comparisons\n";
  return failed ? 1 : 0;
}
}
int main(int argc, char** argv) {
  try { return run(argc, argv); }
  catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
