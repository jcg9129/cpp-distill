// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef _WIN32
#include "core/process/process.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <climits>
#include <thread>

namespace distill::detail {
namespace {
struct Handle {
  HANDLE value = nullptr;
  Handle() = default;
  explicit Handle(HANDLE handle) : value(handle) {}
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  ~Handle() { reset(); }
  void reset(HANDLE handle = nullptr) {
    if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
    value = handle;
  }
};
Error winError(const char* operation, DWORD code = GetLastError()) {
  return Error{std::string(operation) + " (Windows error " + std::to_string(code) + ")"};
}
Result<std::wstring> wide(const std::string& text) {
  if (text.empty()) return std::wstring{};
  if (text.size() > INT_MAX) return Error{"argument too long"};
  const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (!size) return winError("UTF-8 conversion");
  std::wstring result(size, L'\0');
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size))
    return winError("UTF-8 conversion");
  return result;
}
std::wstring quote(const std::wstring& arg) {
  if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;
  std::wstring result = L"\"";
  std::size_t slashes = 0;
  for (const auto c : arg) {
    if (c == L'\\') { ++slashes; continue; }
    if (c == L'"') result.append(slashes * 2 + 1, L'\\');
    else result.append(slashes, L'\\');
    slashes = 0;
    result += c;
  }
  result.append(slashes * 2, L'\\');
  result += L'"';
  return result;
}
struct Attributes {
  LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr;
  bool initialized = false;
  ~Attributes() {
    if (initialized) DeleteProcThreadAttributeList(list);
    if (list) HeapFree(GetProcessHeap(), 0, list);
  }
};
struct Readers {
  HANDLE job;
  std::thread out, err;
  std::atomic<bool> failed{false};
  explicit Readers(HANDLE handle) : job(handle) {}
  ~Readers() {
    if (out.joinable() || err.joinable()) {
      TerminateJobObject(job, 1);
      if (out.joinable()) { CancelSynchronousIo(out.native_handle()); out.join(); }
      if (err.joinable()) { CancelSynchronousIo(err.native_handle()); err.join(); }
    }
  }
};
} // namespace
Result<ProcessResult> runNative(const ProcessSpec& spec, const std::filesystem::path& executable) {
  const auto start = std::chrono::steady_clock::now();
  std::wstring commandLine;
  for (const auto& arg : spec.argv) {
    auto converted = wide(arg);
    if (!converted.ok()) return converted.error();
    if (!commandLine.empty()) commandLine += L' ';
    commandLine += quote(converted.value());
  }
  if (commandLine.size() >= 32767) return Error{"Windows command line exceeds 32766 characters"};
  std::error_code ec;
  const auto cwd = std::filesystem::absolute(spec.cwd, ec);
  if (ec) return Error{"cwd: " + ec.message()};
  SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  Handle outRead, outWrite, errRead, errWrite;
  if (!CreatePipe(&outRead.value, &outWrite.value, &security, 0) ||
      !CreatePipe(&errRead.value, &errWrite.value, &security, 0) ||
      !SetHandleInformation(outRead.value, HANDLE_FLAG_INHERIT, 0) ||
      !SetHandleInformation(errRead.value, HANDLE_FLAG_INHERIT, 0)) return winError("pipe setup");
  const auto stdinPath = spec.stdinFile ? cwd / *spec.stdinFile : std::filesystem::path(L"NUL");
  Handle input(CreateFileW(stdinPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (input.value == INVALID_HANDLE_VALUE) return winError("stdin open");
  Handle job(CreateJobObjectW(nullptr, nullptr));
  if (!job.value) return winError("CreateJobObjectW");
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (spec.memoryLimitBytes) {
    limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limits.ProcessMemoryLimit = *spec.memoryLimitBytes;
  }
  if (!SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
    return winError("SetInformationJobObject");
  SIZE_T attributeSize = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
  Attributes attributes;
  attributes.list = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(), 0, attributeSize));
  if (!attributes.list) return Error{"cannot allocate process attributes"};
  if (!InitializeProcThreadAttributeList(attributes.list, 1, 0, &attributeSize)) return winError("process attributes");
  attributes.initialized = true;
  HANDLE inherited[] = {input.value, outWrite.value, errWrite.value};
  if (!UpdateProcThreadAttribute(attributes.list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                 inherited, sizeof(inherited), nullptr, nullptr)) return winError("handle list");
  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = input.value;
  startup.StartupInfo.hStdOutput = outWrite.value;
  startup.StartupInfo.hStdError = errWrite.value;
  startup.lpAttributeList = attributes.list;
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(executable.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
                      CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT,
                      nullptr, cwd.c_str(), &startup.StartupInfo, &process)) return winError("CreateProcessW");
  Handle child(process.hProcess), primaryThread(process.hThread);
  if (!AssignProcessToJobObject(job.value, child.value)) {
    const auto error = winError("AssignProcessToJobObject");
    TerminateProcess(child.value, 1);
    WaitForSingleObject(child.value, INFINITE);
    return error;
  }
  if (ResumeThread(primaryThread.value) == static_cast<DWORD>(-1)) {
    const auto error = winError("ResumeThread");
    TerminateJobObject(job.value, 1);
    WaitForSingleObject(child.value, INFINITE);
    return error;
  }
  primaryThread.reset(); input.reset(); outWrite.reset(); errWrite.reset();
  ProcessResult result;
  Readers readers(job.value);
  auto drain = [&](HANDLE pipe, std::string& text, bool& truncated) {
    char buffer[16384];
    for (;;) {
      DWORD count = 0;
      if (!ReadFile(pipe, buffer, sizeof(buffer), &count, nullptr)) {
        const auto error = GetLastError();
        if (error != ERROR_BROKEN_PIPE && error != ERROR_OPERATION_ABORTED) readers.failed = true;
        break;
      }
      if (!count) break;
      try { appendOutput(text, truncated, buffer, count, spec.maxOutputBytes); }
      catch (...) { readers.failed = true; }
    }
  };
  readers.out = std::thread([&] { drain(outRead.value, result.stdoutText, result.stdoutTruncated); });
  readers.err = std::thread([&] { drain(errRead.value, result.stderrText, result.stderrTruncated); });
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  const auto remaining = std::max<std::chrono::milliseconds::rep>(0, spec.timeout.count() - elapsed.count());
  const auto wait = WaitForSingleObject(child.value, static_cast<DWORD>(std::min<std::chrono::milliseconds::rep>(remaining, INFINITE - 1)));
  if (wait == WAIT_FAILED) return winError("wait process");
  if (wait == WAIT_TIMEOUT) { result.timedOut = true; TerminateJobObject(job.value, 1); }
  WaitForSingleObject(child.value, INFINITE);
  // Readers finish at EOF, or after a grace period if descendants keep pipes open.
  HANDLE threads[] = {readers.out.native_handle(), readers.err.native_handle()};
  const auto sinceStart = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  const auto grace = std::clamp<std::chrono::milliseconds::rep>(spec.timeout.count() - sinceStart.count(), 0, 2000);
  const auto drained = WaitForMultipleObjects(2, threads, TRUE, static_cast<DWORD>(grace));
  if (drained == WAIT_FAILED) return winError("wait readers");
  if (drained == WAIT_TIMEOUT) {
    if (grace < 2000 && !result.timedOut) result.timedOut = true;
    TerminateJobObject(job.value, 1);
  }
  readers.out.join(); readers.err.join();
  if (readers.failed) return Error{"cannot capture process output"};
  DWORD exitCode;
  if (!GetExitCodeProcess(child.value, &exitCode)) return winError("exit code");
  result.exitCode = static_cast<int>(exitCode);
  result.wall = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  return result;
}
} // namespace distill::detail
#endif
