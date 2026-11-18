// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/command/command.h"
#include "core/path.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <string_view>

namespace distill {
namespace {
bool space(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }
Result<std::vector<std::string>> tokenize(std::string_view text) {
  if (text.find('\0') != std::string_view::npos) return Error{"NUL in response file"};
  std::vector<std::string> words;
  std::string word;
  char quote = 0;
  bool started = false;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '\\') {
      if (++i == text.size()) return Error{"trailing escape in response file"};
      word += text[i];
      started = true;
    } else if (quote) {
      if (c == quote) quote = 0;
      else word += c;
    } else if (c == '\'' || c == '"') {
      quote = c;
      started = true;
    } else if (space(c)) {
      if (started) { words.push_back(std::move(word)); word.clear(); started = false; }
    } else { word += c; started = true; }
  }
  if (quote) return Error{"unterminated quote in response file"};
  if (started) words.push_back(std::move(word));
  return words;
}
Result<std::vector<std::string>> expand(const std::vector<std::string>& argv,
    const std::filesystem::path& cwd, unsigned depth,
    std::vector<std::filesystem::path>& active) {
  std::vector<std::string> result;
  for (const auto& word : argv) {
    if (!word.starts_with('@')) { result.push_back(word); continue; }
    if (depth >= 16) return Error{"response file nesting exceeds 16"};
    std::error_code ec;
    const auto file = std::filesystem::weakly_canonical(cwd / pathFromUtf8(word.substr(1)), ec);
    if (ec) return Error{"response file: " + word + ": " + ec.message()};
    if (std::find(active.begin(), active.end(), file) != active.end())
      return Error{"response file cycle: " + word};
    std::ifstream input(file, std::ios::binary);
    if (!input) return Error{"cannot read response file: " + word};
    const std::string contents((std::istreambuf_iterator<char>(input)), {});
    if (input.bad()) return Error{"cannot read response file: " + word};
    auto tokens = tokenize(contents);
    if (!tokens.ok()) return tokens.error();
    active.push_back(file);
    auto nested = expand(tokens.value(), cwd, depth + 1, active);
    active.pop_back();
    if (!nested.ok()) return nested.error();
    result.insert(result.end(), nested.value().begin(), nested.value().end());
  }
  return result;
}
bool oneOf(std::string_view s, std::initializer_list<std::string_view> values) {
  return std::find(values.begin(), values.end(), s) != values.end();
}
bool warning(std::string_view s) {
  if (!s.starts_with("-W") || s.size() < 3) return false;
  const auto first = s[2];
  const auto alnum = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
  if (!alnum(first)) return false;
  return std::all_of(s.begin() + 3, s.end(), [&](char c) {
    return alnum(c) || c == '+' || c == '=' || c == '_' || c == '-';
  });
}
bool rejected(std::string_view s) {
  if (oneOf(s, {"-c", "-S", "-E", "-Xpreprocessor", "-Xlinker", "-Xassembler", "-M", "-MM", "--sysroot", "-specs", "--specs"}))
    return true;
  // These are specific GCC options with joined arguments, not broad flag families.
  for (auto prefix : {"-include", "-imacros", "-Wp,", "-Wl,", "-Wa,", "-B", "-isysroot", "--sysroot=", "-specs=", "--specs=", "-fplugin="})
    if (s.starts_with(prefix)) return true;
  return s == "-save-temps" || s.starts_with("-save-temps=");
}
Result<CompileCommand> parse(std::vector<std::string> argv, std::filesystem::path cwd) {
  if (argv.empty() || argv[0].empty()) return Error{"missing compiler"};
  for (const auto& word : argv)
    if (word.find('\0') != std::string::npos) return Error{"NUL in compile command"};
  std::error_code ec;
  cwd = std::filesystem::absolute(cwd, ec);
  if (ec) return Error{"invalid working directory: " + ec.message()};
  CompileCommand cmd{pathFromUtf8(argv[0]), {}, cwd.lexically_normal(), {}};
  std::vector<std::filesystem::path> active;
  auto expanded = expand({argv.begin() + 1, argv.end()}, cmd.cwd, 0, active);
  if (!expanded.ok()) return expanded.error();
  argv = std::move(expanded.value());
  for (const auto& word : argv)
    if (word.find('\0') != std::string::npos) return Error{"NUL in expanded compile command"};
  for (std::size_t i = 0; i < argv.size(); ++i) {
    const auto& word = argv[i];
    if (rejected(word)) return Error{"rejected option: " + word};
    Arg arg{{word}, Class::unmodelled, word, {}};
    if (word.empty() || word[0] != '-') {
      auto path = pathFromUtf8(word);
      const auto ext = pathToUtf8(path.extension());
      if (!oneOf(ext, {".cpp", ".cc", ".cxx", ".c++", ".C"}))
        return Error{"unsupported input: " + word};
      if (!cmd.source.empty()) return Error{"more than one input: " + word};
      cmd.source = path;
      arg.classification = Class::input;
      arg.option.clear();
      arg.value = word;
    } else {
      bool matched = false;
      for (auto option : {"-isystem", "-iquote", "-I", "-D", "-U", "-o", "-MF", "-MT", "-l", "-L"}) {
        const std::string_view opt(option);
        if (word == opt || (word.starts_with(opt) && word.size() > opt.size())) {
          arg.option = opt;
          if (word == opt) {
            if (++i == argv.size() || argv[i].empty()) return Error{"missing value for " + word};
            arg.value = argv[i];
            arg.spelling.push_back(argv[i]);
          } else arg.value = word.substr(opt.size());
          if (opt == "-o" || opt == "-MF" || opt == "-MT") arg.classification = Class::controlled;
          else if (opt == "-l") arg.classification = oneOf(arg.value, {"m", "pthread", "stdc++"}) ? Class::harmless : Class::libraries;
          else if (opt == "-L") arg.classification = Class::libraries;
          else arg.classification = Class::modelled;
          matched = true;
          break;
        }
      }
      if (!matched) {
        for (auto option : {"-std=", "-march=", "-mtune=", "-mcpu=", "-fdiagnostics-color=", "-fmax-errors=", "-fsanitize="}) {
          const std::string_view opt(option);
          if (word.starts_with(opt) && word.size() > opt.size()) {
            arg.option = opt;
            arg.value = word.substr(opt.size());
            arg.classification = oneOf(opt, {"-std=", "-march=", "-mtune=", "-mcpu="}) ? Class::modelled : Class::harmless;
            matched = true;
            break;
          }
        }
      }
      if (!matched) {
        if (oneOf(word, {"-m32", "-m64", "-funsigned-char", "-fsigned-char", "-fshort-wchar", "-fno-exceptions", "-fno-rtti", "-fmodules", "-fopenmp"}))
          arg.classification = Class::modelled;
        else if (oneOf(word, {"-MD", "-MMD"})) arg.classification = Class::controlled;
        else if (warning(word) || oneOf(word, {"-O0", "-O1", "-O2", "-O3", "-Os", "-Og", "-Ofast", "-g", "-g0", "-g1", "-g2", "-g3", "-w", "-pedantic", "-pedantic-errors", "-pthread", "-static", "-static-libstdc++", "-static-libgcc", "-s", "-pipe"}))
          arg.classification = Class::harmless;
      }
    }
    cmd.args.push_back(std::move(arg));
  }
  if (cmd.source.empty()) return Error{"missing C++ input"};
  return cmd;
}
} // namespace
Result<CompileCommand> parseCompileCommand(std::vector<std::string> argv, std::filesystem::path cwd) {
  try { return parse(std::move(argv), std::move(cwd)); }
  catch (const std::exception& e) { return Error{std::string("compile command: ") + e.what()}; }
}
std::vector<IncludePath> CompileCommand::userIncludePaths() const {
  std::vector<IncludePath> result;
  for (const auto& arg : args)
    if (arg.option == "-I" || arg.option == "-iquote")
      result.push_back({pathFromUtf8(arg.value), arg.option == "-I" ? IncludeKind::user : IncludeKind::quote});
  return result;
}
std::vector<IncludePath> CompileCommand::systemIncludePaths() const {
  std::vector<IncludePath> result;
  for (const auto& arg : args)
    if (arg.option == "-isystem") result.push_back({pathFromUtf8(arg.value), IncludeKind::system});
  return result;
}
std::vector<LocalMacro> CompileCommand::localMacros() const {
  std::vector<LocalMacro> result;
  for (const auto& arg : args)
    if (arg.option == "-D" || arg.option == "-U") result.push_back({arg.option, arg.value});
  return result;
}
std::optional<std::string> CompileCommand::dialect() const {
  std::optional<std::string> result;
  for (const auto& arg : args) if (arg.option == "-std=") result = arg.value;
  return result;
}
std::vector<Arg> CompileCommand::flags(Class classification) const {
  std::vector<Arg> result;
  for (const auto& arg : args) if (arg.classification == classification) result.push_back(arg);
  return result;
}
Permission CompileCommand::resolutionAllowed() const {
  Permission result{true, {}};
  for (const auto& arg : args) if (arg.classification == Class::unmodelled && !arg.profilePreprocessingInert)
    result.reasons.push_back("unmodelled option: " + arg.option);
  result.allowed = result.reasons.empty();
  return result;
}
Permission CompileCommand::pruningAllowed() const {
  auto result = resolutionAllowed();
  for (const auto& arg : args) if (arg.classification == Class::libraries)
    result.reasons.push_back("external library option: " + arg.spelling.front());
  result.allowed = result.reasons.empty();
  return result;
}
std::vector<std::string> CompileCommand::buildArgv(const std::filesystem::path& newSource,
    const std::filesystem::path& output, bool keepLocalMacros,
    const std::vector<std::string>& extraDefines, bool generatedFile) const {
  std::vector<std::string> result{pathToUtf8(compiler)};
  bool wroteOutput = false;
  for (const auto& arg : args) {
    if (arg.classification == Class::input) { result.push_back(pathToUtf8(newSource)); continue; }
    if (arg.option == "-o") {
      if (!wroteOutput) { result.insert(result.end(), {"-o", pathToUtf8(output)}); wroteOutput = true; }
      continue;
    }
    if (arg.classification == Class::controlled) continue;
    if (generatedFile && (arg.option == "-I" || arg.option == "-iquote")) continue;
    if (!keepLocalMacros && (arg.option == "-D" || arg.option == "-U")) continue;
    result.insert(result.end(), arg.spelling.begin(), arg.spelling.end());
  }
  if (!wroteOutput) result.insert(result.end(), {"-o", pathToUtf8(output)});
  for (const auto& define : extraDefines) result.push_back("-D" + define);
  return result;
}
} // namespace distill
