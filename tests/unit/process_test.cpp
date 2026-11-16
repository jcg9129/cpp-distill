// SPDX-License-Identifier: GPL-3.0-or-later
#include "support.h"
#include "core/process/process.h"
#include <future>
#ifdef _WIN32
#include <cstdint>
#include <sstream>
#include <windows.h>
#else
#include <csignal>
#endif

using namespace distill;
using namespace std::chrono_literals;
namespace {
ProcessSpec spec(std::vector<std::string> args) {
  args.insert(args.begin(), pathToUtf8(test::helper));
  ProcessSpec result;
  result.argv = std::move(args);
  result.cwd = std::filesystem::current_path();
  result.timeout = 10s;
  return result;
}
ProcessResult run(const ProcessSpec& process) {
  auto result = runProcess(process);
  if (!result.ok()) throw std::runtime_error(result.error().message);
  return result.value();
}
} // namespace
TEST(process_argv_roundtrip) {
  const std::vector<std::string> args{"plain", "two words", "\"quoted\"", "back\\slash", "ends with\\", "two\\\\\"quotes", "", "tab\there", "日本語-é", "a\nb"};
  auto words = args;
  words.insert(words.begin(), "echo-args");
  const auto result = run(spec(words));
  std::string expected;
  for (const auto& arg : args) expected += std::to_string(arg.size()) + ':' + arg + '\n';
  CHECK_EQ(result.exitCode, 0);
  CHECK_EQ(result.stdoutText, expected);
  CHECK(result.stderrText.empty());
  CHECK(!result.timedOut);
}
TEST(process_exit_codes) {
  for (const auto code : {0, 1, 42, 123, 127}) CHECK_EQ(run(spec({"exit", std::to_string(code)})).exitCode, code);
}
TEST(process_signal_death) {
#ifdef _WIN32
  throw test::Skip{"POSIX signal status"};
#else
  const auto result = run(spec({"signal"}));
  CHECK_EQ(result.signal.value(), SIGTERM);
  CHECK_EQ(result.exitCode, 128 + SIGTERM);
#endif
}
TEST(process_stdin_file_and_null) {
  test::TempDirectory temp;
  const std::string text = std::string("hello\nworld\r\n") + '\0' + '\x1a' + "binary";
  temp.write("input.txt", text);
  auto process = spec({"cat-stdin"});
  process.cwd = temp.path;
  process.stdinFile = "input.txt";
  CHECK_EQ(run(process).stdoutText, text);
  process.stdinFile.reset();
  CHECK(run(process).stdoutText.empty());
  process.stdinFile = "missing";
  CHECK(!runProcess(process).ok());
}
TEST(process_timeout) {
  auto process = spec({"sleep", "100000"});
  process.timeout = 150ms;
  const auto result = run(process);
  CHECK(result.timedOut);
  CHECK(result.wall < 4s);
  CHECK(result.exitCode != 0);
}
TEST(process_timeout_kills_grandchild_holding_pipe) {
  test::TempDirectory temp;
  const auto marker = temp.path / "survived";
  auto process = spec({"spawn-sleeper", "800", pathToUtf8(marker)});
  process.timeout = 150ms;
  const auto result = run(process);
  CHECK(result.timedOut);
  CHECK_EQ(result.stdoutText, "started\n");
  CHECK(result.wall < 4s);
  run(spec({"sleep", "900"}));
  CHECK(!std::filesystem::exists(marker));
}
TEST(process_exit_grace_kills_descendants) {
  auto process = spec({"spawn-sleeper", "100000"});
  const auto result = run(process);
  CHECK_EQ(result.exitCode, 0);
  CHECK_EQ(result.stdoutText, "started\n");
  CHECK(!result.timedOut);
  CHECK(result.wall < 5s);
}
TEST(process_windows_cancels_readers_with_external_pipe_holders) {
#ifdef _WIN32
  const auto result = run(spec({"export-pipe-handles", std::to_string(GetCurrentProcessId())}));
  std::uintptr_t out = 0, err = 0;
  std::istringstream handles(result.stdoutText);
  handles >> out >> err;
  if (out) CloseHandle(reinterpret_cast<HANDLE>(out));
  if (err) CloseHandle(reinterpret_cast<HANDLE>(err));
  CHECK(out != 0); CHECK(err != 0);
  CHECK_EQ(result.exitCode, 0);
  CHECK(!result.timedOut);
  CHECK(result.stdoutTruncated); CHECK(result.stderrTruncated);
  CHECK(result.wall < 5s);
#else
  throw test::Skip{"Windows synchronous pipe cancellation"};
#endif
}
TEST(process_output_cap_drains_both_streams) {
  auto process = spec({"flood", "10485760"});
  process.maxOutputBytes = 4096;
  const auto result = run(process);
  CHECK_EQ(result.exitCode, 0);
  CHECK(result.stdoutTruncated);
  CHECK(result.stderrTruncated);
  CHECK_EQ(result.stdoutText, std::string(4096, 'x'));
  CHECK_EQ(result.stderrText, std::string(4096, 'x'));
}
TEST(process_output_cap_exact_and_zero) {
  auto process = spec({"flood", "8192"});
  process.maxOutputBytes = 8192;
  auto result = run(process);
  CHECK(!result.stdoutTruncated); CHECK(!result.stderrTruncated);
  process.maxOutputBytes = 0;
  result = run(process);
  CHECK(result.stdoutTruncated); CHECK(result.stderrTruncated);
  CHECK(result.stdoutText.empty()); CHECK(result.stderrText.empty());
}
TEST(process_cwd_and_unicode_executable) {
  test::TempDirectory temp;
  const auto directory = temp.path / pathFromUtf8("space 日本語");
  std::filesystem::create_directory(directory);
  const auto executable = directory / test::helper.filename();
  // Stream the copy: some shared/overlay filesystems reject copy_file's
  // kernel-assisted copy even though ordinary reads and writes work.
  {
    std::ifstream source(test::helper, std::ios::binary);
    std::ofstream destination(executable, std::ios::binary);
    destination << source.rdbuf();
    if (!destination) throw std::runtime_error("cannot copy test helper");
  }
  std::filesystem::permissions(executable, std::filesystem::status(test::helper).permissions());
  auto process = spec({"pwd"});
  process.cwd = directory;
  process.argv[0] = pathToUtf8(std::filesystem::path(".") / executable.filename());
  CHECK_EQ(run(process).stdoutText, pathToUtf8(std::filesystem::canonical(directory)) + '\n');
}
TEST(process_explicit_path_lookup) {
  const auto found = resolveExecutable(test::helper, std::filesystem::current_path());
  CHECK(found.ok());
  CHECK_EQ(found.value(), test::helper);
#ifdef _WIN32
  auto noExtension = test::helper;
  noExtension.replace_extension();
  CHECK(resolveExecutable(noExtension, std::filesystem::current_path()).ok());
  CHECK(resolveExecutable("cmd", std::filesystem::current_path()).ok());
#else
  CHECK(resolveExecutable("g++", std::filesystem::current_path()).ok());
#endif
}
TEST(process_running_executable) {
  const auto self = runningExecutable();
  CHECK(self.ok());
  CHECK(self.value().is_absolute());
  auto expected = test::helper.parent_path() / "distill_unit_tests";
#ifdef _WIN32
  expected += ".exe";
#endif
  CHECK(std::filesystem::equivalent(self.value(), expected));
  auto process = spec({"self-path"});
  process.argv[0] = "distill-custom-argv0-not-on-path-17931";
  auto child = detail::runNative(process, test::helper);
  CHECK(child.ok());
  CHECK_EQ(child.value().exitCode, 0);
  auto output = child.value().stdoutText;
  CHECK(!output.empty() && output.back() == '\n');
  if (!output.empty()) output.pop_back();
  CHECK(std::filesystem::equivalent(pathFromUtf8(output), test::helper));
}
TEST(process_missing_executable_and_bad_specs) {
  auto process = spec({"pwd"});
  process.argv[0] = "distill-definitely-not-an-executable-8239";
  CHECK(!runProcess(process).ok());
  process.argv.clear();
  CHECK(!runProcess(process).ok());
  process = spec({"pwd"}); process.timeout = -1ms;
  CHECK(!runProcess(process).ok());
  process = spec({"echo-args", std::string("x\0y", 3)});
  CHECK(!runProcess(process).ok());
  process = spec({"pwd"}); process.cwd /= "missing-distill-directory";
  CHECK(!runProcess(process).ok());
}
TEST(process_thread_safety) {
  std::vector<std::future<Result<ProcessResult>>> pending;
  for (int i = 0; i < 6; ++i)
    pending.push_back(std::async(std::launch::async, [i] { return runProcess(spec({"echo-args", std::to_string(i)})); }));
  for (int i = 0; i < 6; ++i) {
    const auto result = pending[i].get();
    CHECK(result.ok());
    CHECK_EQ(result.value().stdoutText, "1:" + std::to_string(i) + '\n');
  }
}
TEST(process_exec_failures_are_errors) {
#ifdef _WIN32
  throw test::Skip{"POSIX exec failure reporting"};
#else
  test::TempDirectory temp;
  auto process = spec({"exit", "0"});
  process.cwd = temp.path;
  // Call the native runner to exercise ENOENT/EACCES after executable lookup.
  CHECK(!detail::runNative(process, temp.path / "missing").ok());
  temp.write("denied", "not executable\n");
  std::filesystem::permissions(temp.path / "denied", std::filesystem::perms::owner_read);
  CHECK(!detail::runNative(process, temp.path / "denied").ok());
  // An executable text file must fail execve, without a shell interpreting it.
  temp.write("invalid", "exit 0\n");
  std::filesystem::permissions(temp.path / "invalid", std::filesystem::perms::owner_all);
  process.argv[0] = pathToUtf8(temp.path / "invalid");
  CHECK(!runProcess(process).ok());
#endif
}
TEST(process_memory_limit_smoke) {
  auto process = spec({"exit", "0"});
  process.memoryLimitBytes = 256 * 1024 * 1024;
  CHECK_EQ(run(process).exitCode, 0);
}
