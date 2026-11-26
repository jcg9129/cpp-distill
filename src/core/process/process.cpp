// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/process/process.h"
#include "core/path.h"
#include <cstdlib>
#include <cstdint>
#include <algorithm>
#include <atomic>
#include <future>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

namespace distill {
namespace {
bool executableFile(const std::filesystem::path& path) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) return false;
#ifdef _WIN32
  return true;
#else
  return access(path.c_str(), X_OK) == 0;
#endif
}
Result<std::filesystem::path> resolve(const std::filesystem::path& executable,
                                     const std::filesystem::path& cwd) {
  if (executable.empty()) return Error{"empty executable"};
  const auto name = pathToUtf8(executable);
  if (name.find('\0') != std::string::npos) return Error{"NUL in executable"};
  std::error_code ec;
  const auto base = std::filesystem::absolute(cwd, ec);
  if (ec) return Error{"invalid cwd: " + ec.message()};
  auto candidate = [&](const std::filesystem::path& path) -> std::optional<std::filesystem::path> {
    auto full = (base / path).lexically_normal();
    if (executableFile(full)) return full;
#ifdef _WIN32
    if (full.extension().empty()) {
      full += L".exe";
      if (executableFile(full)) return full;
    }
#endif
    return std::nullopt;
  };
  if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos || executable.has_root_name()) {
    if (auto found = candidate(executable)) return *found;
    return Error{"executable not found: " + name};
  }
  std::string paths;
#ifdef _WIN32
  const auto size = GetEnvironmentVariableW(L"PATH", nullptr, 0);
  if (size) {
    std::wstring buffer(size, L'\0');
    const auto copied = GetEnvironmentVariableW(L"PATH", buffer.data(), size);
    if (copied >= size) return Error{"PATH changed during lookup"};
    buffer.resize(copied);
    paths = pathToUtf8(std::filesystem::path(buffer));
  }
  constexpr char separator = ';';
#else
  if (const char* value = std::getenv("PATH")) paths = value;
  constexpr char separator = ':';
#endif
  std::size_t begin = 0;
  do {
    auto end = paths.find(separator, begin);
    auto entry = paths.substr(begin, end == std::string::npos ? end : end - begin);
#ifdef _WIN32
    if (entry.size() >= 2 && entry.front() == '"' && entry.back() == '"') entry = entry.substr(1, entry.size() - 2);
#endif
    if (auto found = candidate(pathFromUtf8(entry) / executable)) return *found;
    if (end == std::string::npos) break;
    begin = end + 1;
  } while (true);
  return Error{"executable not found on PATH: " + name};
}
} // namespace
Result<std::filesystem::path> runningExecutable() {
  try {
#ifdef _WIN32
    for (DWORD size = 1024; size <= 32768; size *= 2) {
      std::wstring buffer(size, L'\0');
      const auto copied = GetModuleFileNameW(nullptr, buffer.data(), size);
      if (!copied) return Error{"GetModuleFileNameW failed"};
      if (copied < size) { buffer.resize(copied); return std::filesystem::path(buffer); }
    }
    return Error{"running executable path too long"};
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return Error{"_NSGetExecutablePath failed"};
    return std::filesystem::canonical(buffer.data());
#elif defined(__linux__)
    return std::filesystem::read_symlink("/proc/self/exe");
#else
    return Error{"running executable lookup unsupported"};
#endif
  } catch (const std::exception& e) { return Error{std::string("running executable: ") + e.what()}; }
}
Result<std::filesystem::path> resolveExecutable(const std::filesystem::path& executable,
                                               const std::filesystem::path& cwd) {
  try { return resolve(executable, cwd); }
  catch (const std::exception& e) { return Error{std::string("executable lookup: ") + e.what()}; }
}
std::vector<Result<ProcessResult>> runProcessBatch(const std::vector<ProcessSpec>& specs,
                                                  unsigned jobs, ProcessRunner runner) {
  std::vector<Result<ProcessResult>> results(specs.size(), Error{"process not started"});
  std::atomic<std::size_t> next{0};
  auto work = [&] {
    for (;;) {
      const auto i = next.fetch_add(1);
      if (i >= specs.size()) return;
      try { results[i] = runner(specs[i]); }
      catch (const Error& e) { results[i] = e; }
      catch (const std::exception& e) { results[i] = Error{e.what()}; }
      catch (...) { results[i] = Error{"process runner threw an unknown exception"}; }
    }
  };
  std::vector<std::future<void>> workers;
  const auto count = std::min<std::size_t>(std::max(1u, jobs), specs.size());
  for (std::size_t i = 1; i < count; ++i) workers.push_back(std::async(std::launch::async, work));
  work();
  for (auto& worker : workers) worker.get();
  return results;
}
Result<ProcessResult> runProcess(const ProcessSpec& spec) {
  try {
    if (spec.argv.empty()) return Error{"empty process argv"};
    for (const auto& arg : spec.argv)
      if (arg.find('\0') != std::string::npos) return Error{"NUL in process argument"};
    if (spec.timeout.count() < 0) return Error{"negative process timeout"};
    auto executable = resolveExecutable(pathFromUtf8(spec.argv[0]), spec.cwd);
    if (!executable.ok()) return executable.error();
    return detail::runNative(spec, executable.value());
  } catch (const std::exception& e) { return Error{std::string("process: ") + e.what()}; }
}
} // namespace distill
