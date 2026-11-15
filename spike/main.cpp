// SPDX-License-Identifier: GPL-3.0-or-later
#include "resources.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace {
// Drive-qualified on Windows: VFS and the driver must agree on absolute paths.
#ifdef _WIN32
constexpr const char *resourceDir = "C:/__cpp_distill_resource__";
#else
constexpr const char *resourceDir = "/__cpp_distill_resource__";
#endif

std::string trim(std::string text) {
  const auto begin = text.find_first_not_of(" \r\n\t");
  if (begin == std::string::npos)
    return {};
  return text.substr(begin, text.find_last_not_of(" \r\n\t") - begin + 1);
}

struct TempDirectory {
  llvm::SmallString<128> path;
  bool create() {
    if (auto error = llvm::sys::fs::createUniqueDirectory("distill-probe", path)) {
      llvm::errs() << "temporary directory: " << error.message() << '\n';
      return false;
    }
    return true;
  }
  std::string file(llvm::StringRef name) const {
    auto result = path;
    llvm::sys::path::append(result, name);
    return result.str().str();
  }
  ~TempDirectory() {
    if (!path.empty())
      if (auto error = llvm::sys::fs::remove_directories(path))
        llvm::errs() << "temporary cleanup: " << error.message() << '\n';
  }
};

bool execute(const std::vector<std::string> &arguments, const std::string &out,
             const std::string &err) {
  std::vector<llvm::StringRef> refs(arguments.begin(), arguments.end());
  const std::optional<llvm::StringRef> redirects[] = {std::nullopt, out, err};
  std::string message;
  bool failed = false;
  const int result = llvm::sys::ExecuteAndWait(arguments.front(), refs,
      std::nullopt, redirects, 30, 0, &message, &failed);
  if (result != 0 || failed) {
    llvm::errs() << "compiler probe failed (" << result << "): " << message << '\n';
    auto diagnostics = llvm::MemoryBuffer::getFile(err);
    if (diagnostics)
      llvm::errs() << (*diagnostics)->getBuffer();
    return false;
  }
  return true;
}

struct IncludeDir {
  std::string path;
  bool quote;
  bool framework;
};
struct Probe {
  std::string triple;
  std::vector<IncludeDir> includes;
};

bool gccInternal(std::string path) {
  std::replace(path.begin(), path.end(), '\\', '/');
  // GCC often prints lib/gcc/<triple>/<version>/../../../../<triple>/include.
  // Normalize before classification: that path is a target C library directory,
  // not a builtin directory, even though the unnormalized text contains /gcc/.
  constexpr auto style = llvm::sys::path::Style::posix;
  llvm::SmallString<256> normalized(path);
  llvm::sys::path::remove_dots(normalized, true, style);
  auto parent = llvm::sys::path::parent_path(normalized, style);
  const auto leaf = llvm::sys::path::filename(normalized, style);
  if (leaf != "include" && leaf != "include-fixed")
    return false;
  const auto version = llvm::sys::path::filename(parent, style);
  parent = llvm::sys::path::parent_path(parent, style);
  const auto triple = llvm::sys::path::filename(parent, style);
  parent = llvm::sys::path::parent_path(parent, style);
  const auto gcc = llvm::sys::path::filename(parent, style);
  // Keep include/c++, target system headers and Homebrew's other GCC paths.
  return (gcc == "gcc" || gcc == "gcc-cross") && triple.contains('-') &&
         !version.empty() && version.front() >= '0' && version.front() <= '9';
}

bool probeCompiler(const std::string &compiler, Probe &probe) {
  auto program = llvm::sys::findProgramByName(compiler);
  if (!program) {
    llvm::errs() << "compiler: " << program.getError().message() << '\n';
    return false;
  }
  TempDirectory temp;
  if (!temp.create())
    return false;
  const auto input = temp.file("empty.cpp"), output = temp.file("output.txt"),
             errors = temp.file("stderr.txt");
  std::error_code error;
  { llvm::raw_fd_ostream empty(input, error); }
  if (error) {
    llvm::errs() << "cannot create probe input: " << error.message() << '\n';
    return false;
  }
  if (!execute({*program, "-xc++", "-E", "-v", input, "-o", temp.file("empty.i")}, output, errors))
    return false;
  auto buffer = llvm::MemoryBuffer::getFile(errors);
  if (!buffer) {
    llvm::errs() << "cannot read compiler search paths\n";
    return false;
  }
  std::istringstream lines((*buffer)->getBuffer().str());
  std::string line;
  bool inList = false, quote = false, sawList = false, sawEnd = false;
  while (std::getline(lines, line)) {
    line = trim(line);
    if (line == "#include \"...\" search starts here:") {
      inList = quote = sawList = true;
    } else if (line == "#include <...> search starts here:") {
      inList = sawList = true;
      quote = false;
    } else if (line == "End of search list.") {
      inList = false;
      sawEnd = true;
    } else if (inList && !line.empty()) {
      constexpr std::string_view suffix = " (framework directory)";
      const bool framework = line.ends_with(suffix);
      if (framework)
        line.resize(line.size() - suffix.size());
      if (gccInternal(line))
        llvm::outs() << "filtered GCC builtin directory: " << line << '\n';
      else
        probe.includes.push_back({line, quote, framework});
    }
  }
  if (!sawList || !sawEnd || probe.includes.empty()) {
    llvm::errs() << "compiler did not produce a complete include search list\n";
    return false;
  }
  if (!execute({*program, "-dumpmachine"}, output, errors))
    return false;
  buffer = llvm::MemoryBuffer::getFile(output);
  if (!buffer)
    return false;
  probe.triple = trim((*buffer)->getBuffer().str());
  if (probe.triple.empty() || probe.triple.find_first_of(" \t\r\n") != std::string::npos) {
    llvm::errs() << "invalid compiler target triple\n";
    return false;
  }
  return true;
}

class Diagnostics final : public clang::DiagnosticConsumer {
public:
  void HandleDiagnostic(clang::DiagnosticsEngine::Level level,
                        const clang::Diagnostic &diagnostic) override {
    clang::DiagnosticConsumer::HandleDiagnostic(level, diagnostic);
    llvm::SmallString<256> message;
    diagnostic.FormatDiagnostic(message);
    llvm::errs() << (level >= clang::DiagnosticsEngine::Error ? "error: " :
                    level == clang::DiagnosticsEngine::Warning ? "warning: " : "note: ")
                 << message << '\n';
  }
};

std::uint64_t peakRSS() {
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS counters{};
  if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
    return counters.PeakWorkingSetSize;
#else
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
#ifdef __APPLE__
    return usage.ru_maxrss;
#else
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024;
#endif
  }
#endif
  return 0;
}

int run(const std::vector<std::string> &args) {
  const auto start = std::chrono::steady_clock::now();
  if (args.size() == 2 && args[1] == "--list-resources") {
    std::size_t total = 0;
    for (const auto &resource : distill::resources()) {
      llvm::outs() << resource.path << '\n';
      total += resource.size;
    }
    llvm::outs() << "total bytes: " << total << '\n';
    return 0;
  }
  const bool self = args.size() == 2 && args[1] == "--self-check";
  const bool foreign = args.size() == 3 && args[1] == "--foreign-check";
  if (!self && !foreign && args.size() < 3) {
    llvm::errs() << "usage: distill-stub <compiler> <file.cpp> [extra args...]\n"
                    "       distill-stub --self-check | --list-resources\n"
                    "       distill-stub --foreign-check <file.cpp>\n";
    return 2;
  }
  Probe probe;
  if (self)
    probe.triple = llvm::sys::getDefaultTargetTriple();
  else if (foreign)
    probe.triple = "x86_64-w64-mingw32";
  else if (!probeCompiler(args[1], probe))
    return 1;
  llvm::outs() << "triple: " << probe.triple << '\n';

  auto memory = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  for (const auto &resource : distill::resources()) {
    const std::string path = std::string(resourceDir) + "/include/" + resource.path;
    const llvm::StringRef data(reinterpret_cast<const char *>(resource.data), resource.size);
    if (!memory->addFile(path, 0, llvm::MemoryBuffer::getMemBuffer(data, path))) {
      llvm::errs() << "cannot install embedded header: " << path << '\n';
      return 1;
    }
  }
  auto overlay = llvm::makeIntrusiveRefCnt<llvm::vfs::OverlayFileSystem>(llvm::vfs::getRealFileSystem());
  overlay->pushOverlay(memory);
  const std::string source = self ? std::string(resourceDir) + "/self-check.cpp" : args[2];
  if (self) {
    // Freestanding makes resource stdint.h define its own integer types instead
    // of include_next into the host's C library. No compiler or host headers.
    constexpr llvm::StringLiteral code =
        "#include <stddef.h>\n#include <stdarg.h>\n#include <stdint.h>\n"
        "static_assert(sizeof(size_t) == sizeof(void*));\n"
        "int f(int n, ...) { va_list a; va_start(a,n); int x=va_arg(a,int); "
        "va_end(a); return (int)(uint32_t)x; }\n";
    memory->addFile(source, 0, llvm::MemoryBuffer::getMemBuffer(code));
  }
  std::vector<std::string> command = {"distill-clang", "-fsyntax-only", "-xc++",
      "-std=gnu++20", "-nostdinc++", "-nostdlibinc", "-target", probe.triple,
      std::string("-resource-dir=") + resourceDir,
      // Explicitly first: Clang builtins must precede GCC's include_next chain.
      "-isystem", std::string(resourceDir) + "/include"};
  if (self || foreign)
    command.push_back("-ffreestanding");
  for (const auto &include : probe.includes) {
    llvm::outs() << (include.quote ? "quote: " : include.framework ? "framework: " : "system: ")
                 << include.path << '\n';
    command.push_back(include.framework ? "-iframework" : include.quote ? "-iquote" : "-isystem");
    command.push_back(include.path);
  }
  if (!self && !foreign)
    command.insert(command.end(), args.begin() + 3, args.end());
  command.push_back(source);
  Diagnostics diagnostics;
  // ToolInvocation retains the FileManager in CompilerInstance. Intrusive
  // ownership prevents that temporary retain/release from deleting a stack object.
  auto files = llvm::makeIntrusiveRefCnt<clang::FileManager>(clang::FileSystemOptions{}, overlay);
  clang::tooling::ToolInvocation invocation(command, std::make_unique<clang::SyntaxOnlyAction>(), files.get());
  invocation.setDiagnosticConsumer(&diagnostics);
  const bool success = invocation.run();
  const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  llvm::outs() << "errors: " << diagnostics.getNumErrors() << "\nwarnings: "
               << diagnostics.getNumWarnings() << "\nwall seconds: " << elapsed
               << "\npeak RSS bytes: " << peakRSS() << '\n';
  return success && diagnostics.getNumErrors() == 0 ? 0 : 1;
}
} // namespace

#ifdef _WIN32
// LLVM consumes UTF-8 paths; bypass the MSVC narrow argv codepage conversion.
int wmain(int argc, wchar_t **argv) {
  std::vector<std::string> args;
  for (int i = 0; i < argc; ++i) {
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1,
                                          nullptr, 0, nullptr, nullptr);
    if (!size) {
      llvm::errs() << "argument conversion failed\n";
      return 2;
    }
    std::string utf8(size, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1,
                             utf8.data(), size, nullptr, nullptr))
      return 2;
    utf8.pop_back();
    args.push_back(std::move(utf8));
  }
  return run(args);
}
#else
int main(int argc, char **argv) {
  return run(std::vector<std::string>(argv, argv + argc));
}
#endif
