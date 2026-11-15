// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/probe/probe.h"
#include "core/path.h"
#include "core/process/process.h"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <fstream>
#include <sstream>

namespace distill {
namespace {
std::string_view trim(std::string_view text) {
  const auto begin = text.find_first_not_of(" \t\r\n");
  if (begin == std::string_view::npos) return {};
  return text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
}
bool identifier(std::string_view s) {
  const auto first = [](char c) { return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
  if (s.empty() || !first(s[0])) return false;
  return std::all_of(s.begin() + 1, s.end(), [&](char c) { return first(c) || (c >= '0' && c <= '9'); });
}
long number(const MacroTable& macros, std::string_view name) {
  const auto it = macros.find(name);
  if (it == macros.end()) return 0;
  const auto text = trim(it->second.replacement);
  long value = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr == text.data()) return 0;
  for (auto p = parsed.ptr; p != text.data() + text.size(); ++p)
    if (*p != 'L' && *p != 'U' && *p != 'l' && *p != 'u') return 0;
  return value;
}
Result<SearchLists> searchLists(std::string_view text) {
  SearchLists result;
  enum class State { before, quote, angle, done } state = State::before;
  std::istringstream lines{std::string(text)};
  std::string line;
  while (std::getline(lines, line)) {
    auto content = trim(line);
    if (content == "#include \"...\" search starts here:") {
      if (state != State::before) return Error{"out-of-order include search markers"};
      state = State::quote;
    } else if (content == "#include <...> search starts here:") {
      if (state != State::quote) return Error{"missing quoted include search marker"};
      state = State::angle;
    } else if (content == "End of search list.") {
      if (state != State::angle) return Error{"missing angle include search marker"};
      state = State::done;
    } else if ((state == State::quote || state == State::angle) && !content.empty()) {
      constexpr std::string_view suffix = " (framework directory)";
      const bool framework = content.ends_with(suffix);
      if (framework) content = trim(content.substr(0, content.size() - suffix.size()));
      if (content.empty()) return Error{"empty include search path"};
      const auto path = pathFromUtf8(content);
      result.directories.push_back({path, state == State::quote ? IncludeKind::quote : IncludeKind::system,
                                    framework, isGccInternalIncludeDir(path)});
    }
  }
  if (state != State::done) return Error{"compiler did not produce complete include search markers"};
  return result;
}
Result<MacroTable> macros(std::string_view text) {
  MacroTable result;
  std::istringstream lines{std::string(text)};
  std::string line;
  std::size_t lineNumber = 0;
  while (std::getline(lines, line)) {
    ++lineNumber;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (trim(line).empty()) continue;
    auto malformed = [&] { return Error{"malformed macro on line " + std::to_string(lineNumber) + ": " + line}; };
    std::string_view content(line);
    if (content.find('\0') != std::string_view::npos || !content.starts_with("#define") ||
        content.size() <= 7 || (content[7] != ' ' && content[7] != '\t')) return malformed();
    content.remove_prefix(8);
    const auto nameBegin = content.find_first_not_of(" \t");
    if (nameBegin == std::string_view::npos) return malformed();
    content.remove_prefix(nameBegin);
    const auto nameEnd = content.find_first_of(" (\t");
    const std::string name(content.substr(0, nameEnd));
    if (!identifier(name)) return malformed();
    content.remove_prefix(name.size());
    Macro macro;
    if (content.starts_with('(')) {
      macro.functionLike = true;
      const auto close = content.find(')');
      if (close == std::string_view::npos) return malformed();
      auto params = trim(content.substr(1, close - 1));
      while (!params.empty()) {
        const auto comma = params.find(',');
        const auto param = trim(params.substr(0, comma));
        if (param == "..." || (param.ends_with("...") && identifier(param.substr(0, param.size() - 3))))
          macro.variadic = true;
        else if (!identifier(param)) return malformed();
        if (std::find(macro.parameters.begin(), macro.parameters.end(), param) != macro.parameters.end()) return malformed();
        macro.parameters.emplace_back(param);
        if (comma == std::string_view::npos) break;
        if (macro.variadic) return malformed();
        params = trim(params.substr(comma + 1));
        if (params.empty()) return malformed();
      }
      content.remove_prefix(close + 1);
    }
    if (!content.empty()) {
      if (content.front() != ' ' && content.front() != '\t') return malformed();
      // One whitespace character separates the definition from its replacement;
      // retain all remaining bytes, including additional/trailing whitespace.
      content.remove_prefix(1);
    }
    macro.replacement = content;
    if (!result.emplace(name, std::move(macro)).second) return malformed();
  }
  return result;
}
struct TempDirectory {
  std::filesystem::path path;
  ~TempDirectory() { std::error_code ec; if (!path.empty()) std::filesystem::remove_all(path, ec); }
  Result<std::filesystem::path> create(const std::filesystem::path& root) {
    static std::atomic<unsigned long long> sequence{0};
    for (int attempt = 0; attempt < 100; ++attempt) {
      const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
      auto candidate = root / (".distill-probe-" + std::to_string(stamp) + "-" + std::to_string(sequence++));
      std::error_code ec;
      if (std::filesystem::create_directory(candidate, ec)) { path = candidate; return path; }
      if (ec && ec != std::errc::file_exists) return Error{"temporary directory: " + ec.message()};
    }
    return Error{"cannot create unique probe directory"};
  }
};
Result<ProbeResult> probe(const std::filesystem::path& compiler, const CompileCommand& command,
                          const ProbeOptions& options) {
  TempDirectory temp;
  std::error_code ec;
  const auto root = std::filesystem::absolute(options.temporaryRoot.value_or(command.cwd), ec);
  if (ec) return Error{"temporary root: " + ec.message()};
  auto created = temp.create(root);
  if (!created.ok()) return created.error();
  const auto input = temp.path / "empty.cpp";
  { std::ofstream file(input, std::ios::binary); if (!file) return Error{"cannot create probe source"}; }
  std::vector<std::string> modelled;
  std::vector<std::string> target;
  for (const auto& arg : command.args) {
    if (arg.classification != Class::modelled) continue;
    if (arg.option == "-m32" || arg.option == "-m64") target.insert(target.end(), arg.spelling.begin(), arg.spelling.end());
    if (!options.keepLocalMacros && (arg.option == "-D" || arg.option == "-U")) continue;
    // User include dirs are relevant to inlining, not the compiler's system search.
    if (arg.option == "-I" || arg.option == "-iquote") continue;
    modelled.insert(modelled.end(), arg.spelling.begin(), arg.spelling.end());
  }
  for (const auto& define : options.defines) modelled.push_back("-D" + define);
  auto execute = [&](std::vector<std::string> argv, const std::vector<std::string>& flags) -> Result<ProcessResult> {
    argv.insert(argv.begin(), pathToUtf8(compiler));
    argv.insert(argv.end(), flags.begin(), flags.end());
    ProcessSpec spec;
    spec.argv = std::move(argv);
    spec.cwd = command.cwd;
    spec.timeout = std::chrono::seconds(30);
    auto run = runProcess(spec);
    if (!run.ok()) return run.error();
    if (run.value().timedOut || run.value().exitCode != 0 || run.value().signal)
      return Error{"compiler probe failed: " + run.value().stderrText};
    if (run.value().stdoutTruncated || run.value().stderrTruncated) return Error{"compiler probe output exceeded limit"};
    return run;
  };
  auto verbose = execute({"-xc++", "-E", "-v", pathToUtf8(input), "-o", pathToUtf8(temp.path / "empty.i")}, modelled);
  if (!verbose.ok()) return verbose.error();
  auto lists = parseSearchLists(verbose.value().stderrText);
  if (!lists.ok()) return lists.error();
  auto machine = execute({"-dumpmachine"}, target);
  if (!machine.ok()) return machine.error();
  const std::string triple(trim(machine.value().stdoutText));
  if (triple.empty() || triple.find_first_of(" \t\r\n") != std::string::npos) return Error{"invalid compiler target triple"};
  auto predefined = execute({"-xc++", "-dM", "-E", pathToUtf8(input)}, modelled);
  if (!predefined.ok()) return predefined.error();
  auto table = parseMacros(predefined.value().stdoutText);
  if (!table.ok()) return table.error();
  auto identity = identifyCompiler(table.value());
  auto dialect = effectiveDialect(command.dialect(), table.value());
  return ProbeResult{std::move(lists.value()), triple, std::move(table.value()), identity, std::move(dialect)};
}
} // namespace
bool isGccInternalIncludeDir(const std::filesystem::path& path) {
  auto text = pathToUtf8(path);
  std::replace(text.begin(), text.end(), '\\', '/');
  const auto normalized = pathFromUtf8(text).lexically_normal();
  auto parent = normalized.parent_path();
  const auto leaf = pathToUtf8(normalized.filename());
  if (leaf != "include" && leaf != "include-fixed") return false;
  const auto version = pathToUtf8(parent.filename());
  parent = parent.parent_path();
  const auto triple = pathToUtf8(parent.filename());
  parent = parent.parent_path();
  const auto gcc = pathToUtf8(parent.filename());
  return (gcc == "gcc" || gcc == "gcc-cross") && triple.find('-') != std::string::npos &&
         !version.empty() && version[0] >= '0' && version[0] <= '9';
}
Result<SearchLists> parseSearchLists(std::string_view text) {
  try { return searchLists(text); }
  catch (const std::exception& e) { return Error{std::string("search lists: ") + e.what()}; }
}
Result<MacroTable> parseMacros(std::string_view text) {
  try { return macros(text); }
  catch (const std::exception& e) { return Error{std::string("macro table: ") + e.what()}; }
}
CompilerIdentity identifyCompiler(const MacroTable& macros) {
  CompilerIdentity result;
  if (macros.contains("__clang__")) {
    result.family = macros.contains("__apple_build_version__") ? CompilerFamily::appleClang : CompilerFamily::clang;
    result.major = static_cast<int>(number(macros, "__clang_major__"));
    result.minor = static_cast<int>(number(macros, "__clang_minor__"));
    result.patch = static_cast<int>(number(macros, "__clang_patchlevel__"));
  } else if (macros.contains("__GNUC__")) {
    result.family = CompilerFamily::gcc;
    result.major = static_cast<int>(number(macros, "__GNUC__"));
    result.minor = static_cast<int>(number(macros, "__GNUC_MINOR__"));
    result.patch = static_cast<int>(number(macros, "__GNUC_PATCHLEVEL__"));
  }
  return result;
}
Dialect effectiveDialect(std::optional<std::string> stdFlag, const MacroTable& macros) {
  const auto cpp = number(macros, "__cplusplus");
  const bool gnu = stdFlag ? stdFlag->starts_with("gnu++") : !macros.contains("__STRICT_ANSI__");
  if (stdFlag) {
    if (stdFlag->starts_with("-std=")) *stdFlag = stdFlag->substr(5);
    return {*stdFlag, stdFlag->starts_with("gnu++"), cpp};
  }
  const auto version = cpp > 202302 ? "26" : cpp > 202002 ? "23" : cpp >= 202002 ? "20" :
                       cpp >= 201703 ? "17" : cpp >= 201402 ? "14" : cpp >= 201103 ? "11" : cpp >= 199711 ? "98" : "unknown";
  return {std::string(gnu ? "gnu++" : "c++") + version, gnu, cpp};
}
Result<ProbeResult> probeCompiler(const std::filesystem::path& compiler,
                                 const CompileCommand& command, ProbeOptions options) {
  try { return probe(compiler, command, options); }
  catch (const std::exception& e) { return Error{std::string("compiler probe: ") + e.what()}; }
}
} // namespace distill
