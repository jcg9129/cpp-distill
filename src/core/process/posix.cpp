// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef _WIN32
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "core/process/process.h"
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
namespace distill::detail {
namespace {
using Clock = std::chrono::steady_clock;
#ifndef __linux__
// pipe()+fcntl(CLOEXEC) is not atomic. Keep another runner from spawning in
// between those calls and inheriting a pipe writer until its own exit.
// Linux uses atomic pipe2(O_CLOEXEC) instead.
std::mutex spawnMutex;
#endif
struct Fd {
  int value = -1;
  Fd() = default;
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  ~Fd() { reset(); }
  void reset(int fd = -1) { if (value >= 0) close(value); value = fd; }
};
bool makePipe(Fd& read, Fd& write) {
  int pipeFds[2];
#ifdef __linux__
  if (pipe2(pipeFds, O_CLOEXEC) != 0) return false;
#else
  if (pipe(pipeFds) != 0) return false;
  if (fcntl(pipeFds[0], F_SETFD, FD_CLOEXEC) < 0 || fcntl(pipeFds[1], F_SETFD, FD_CLOEXEC) < 0) {
    close(pipeFds[0]); close(pipeFds[1]); return false;
  }
#endif
  // Keep our descriptors away from stdin/out/err even if the caller closed them.
  for (auto& fd : pipeFds) {
    if (fd <= STDERR_FILENO) {
      int replacement = fcntl(fd, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
      if (replacement < 0) { close(pipeFds[0]); close(pipeFds[1]); return false; }
      close(fd); fd = replacement;
    }
  }
  read.reset(pipeFds[0]); write.reset(pipeFds[1]);
  return true;
}
Error osError(const char* operation, int code = errno) {
  return Error{std::string(operation) + ": " + std::strerror(code)};
}
#ifdef DISTILL_HAVE_ADDCHDIR_NP
struct SpawnSetup {
  posix_spawn_file_actions_t actions{};
  posix_spawnattr_t attr{};
  bool actionsReady = false, attrReady = false;
  ~SpawnSetup() {
    if (actionsReady) posix_spawn_file_actions_destroy(&actions);
    if (attrReady) posix_spawnattr_destroy(&attr);
  }
};
#else
// Called only after fork: no allocation, locks, or C++ runtime cleanup.
[[noreturn]] void execFailure(int fd, int code) {
  const char* data = reinterpret_cast<const char*>(&code);
  std::size_t left = sizeof(code);
  while (left) {
    const auto count = write(fd, data, left);
    if (count > 0) { data += count; left -= static_cast<std::size_t>(count); }
    else if (count < 0 && errno == EINTR) continue;
    else break;
  }
  _exit(127);
}
#endif
struct Child {
  pid_t pid;
  bool reaped = false;
  bool groupMayContainProcesses = true;
  void killGroup() {
    if (groupMayContainProcesses && (kill(-pid, SIGKILL) == 0 || errno == ESRCH))
      groupMayContainProcesses = false;
  }
  ~Child() {
    // Also contains descendants after a successful direct-child exit.
    killGroup();
    if (!reaped) { int status; while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {} }
  }
};
} // namespace
Result<ProcessResult> runNative(const ProcessSpec& spec, const std::filesystem::path& executable) {
  const auto start = Clock::now();
#ifndef __linux__
  std::unique_lock spawnLock(spawnMutex);
#endif
  Fd outRead, outWrite, errRead, errWrite;
  if (!makePipe(outRead, outWrite) || !makePipe(errRead, errWrite)) return osError("pipe");
  if (fcntl(outRead.value, F_SETFL, O_NONBLOCK) < 0 || fcntl(errRead.value, F_SETFL, O_NONBLOCK) < 0)
    return osError("nonblocking pipe");
  std::error_code ec;
  auto cwd = std::filesystem::absolute(spec.cwd, ec);
  if (ec) return Error{"cwd: " + ec.message()};
  const auto stdinPath = spec.stdinFile ? cwd / *spec.stdinFile : std::filesystem::path("/dev/null");
  std::vector<char*> argv;
  for (const auto& arg : spec.argv) argv.push_back(const_cast<char*>(arg.c_str()));
  argv.push_back(nullptr);
  pid_t pid;
#ifdef DISTILL_HAVE_ADDCHDIR_NP
  SpawnSetup setup;
  int rc = posix_spawn_file_actions_init(&setup.actions);
  if (rc) return osError("spawn actions", rc);
  setup.actionsReady = true;
  rc = posix_spawnattr_init(&setup.attr);
  if (rc) return osError("spawn attributes", rc);
  setup.attrReady = true;
  short flags = POSIX_SPAWN_SETPGROUP;
#ifdef __APPLE__
  // Also exclude unrelated descriptors opened concurrently outside this runner.
  // Only the explicitly opened/duplicated stdin/out/err belong in the child.
  flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#endif
  if ((rc = posix_spawn_file_actions_addchdir_np(&setup.actions, cwd.c_str())) ||
      (rc = posix_spawn_file_actions_addopen(&setup.actions, STDIN_FILENO, stdinPath.c_str(), O_RDONLY, 0)) ||
      (rc = posix_spawn_file_actions_adddup2(&setup.actions, outWrite.value, STDOUT_FILENO)) ||
      (rc = posix_spawn_file_actions_adddup2(&setup.actions, errWrite.value, STDERR_FILENO)) ||
      (rc = posix_spawnattr_setflags(&setup.attr, flags)) ||
      (rc = posix_spawnattr_setpgroup(&setup.attr, 0))) return osError("spawn setup", rc);
  rc = posix_spawn(&pid, executable.c_str(), &setup.actions, &setup.attr, argv.data(), environ);
  if (rc) return osError("posix_spawn", rc);
#else
  Fd execRead, execWrite;
  if (!makePipe(execRead, execWrite)) return osError("exec pipe");
  // Snapshot the environment and extract all pointers/descriptors before fork.
  std::vector<std::string> environment;
  for (auto entry = environ; *entry; ++entry) environment.emplace_back(*entry);
  std::vector<char*> envp;
  for (auto& entry : environment) envp.push_back(entry.data());
  envp.push_back(nullptr);
  char* const* args = argv.data();
  char* const* env = envp.data();
  const char* program = executable.c_str();
  const char* directory = cwd.c_str();
  const char* inputPath = stdinPath.c_str();
  const int reportFd = execWrite.value, reportReadFd = execRead.value;
  const int stdoutFd = outWrite.value, stderrFd = errWrite.value;
  pid = fork();
  if (pid < 0) return osError("fork");
  if (pid == 0) {
    close(reportReadFd);
    if (setpgid(0, 0) < 0 || chdir(directory) < 0) execFailure(reportFd, errno);
    const int inputFd = open(inputPath, O_RDONLY);
    if (inputFd < 0) execFailure(reportFd, errno);
    if (dup2(inputFd, STDIN_FILENO) < 0) execFailure(reportFd, errno);
    if (inputFd != STDIN_FILENO) close(inputFd);
    if (dup2(stdoutFd, STDOUT_FILENO) < 0 || dup2(stderrFd, STDERR_FILENO) < 0)
      execFailure(reportFd, errno);
    execve(program, args, env);
    execFailure(reportFd, errno);
  }
#endif
  Child child{pid};
#ifndef __linux__
  // The child has its descriptor snapshot. Never serialize process execution,
  // pipe draining, or waiting for the fork/exec error report under this lock.
  spawnLock.unlock();
#endif
#ifndef DISTILL_HAVE_ADDCHDIR_NP
  execWrite.reset();
  int execError = 0;
  char* data = reinterpret_cast<char*>(&execError);
  std::size_t received = 0;
  while (received < sizeof(execError)) {
    const auto count = read(execRead.value, data + received, sizeof(execError) - received);
    if (count > 0) received += static_cast<std::size_t>(count);
    else if (count == 0) break; // CLOEXEC closes the writer on successful exec.
    else if (errno != EINTR) return osError("exec report read");
  }
  if (received) {
    if (received != sizeof(execError)) return Error{"incomplete exec failure report"};
    return osError("fork/exec setup", execError);
  }
  execRead.reset();
#endif
  outWrite.reset(); errWrite.reset();
#ifdef __linux__
  // prlimit after spawn leaves a small race before the child's limit is applied.
  if (spec.memoryLimitBytes) {
    const auto limitValue = static_cast<rlim_t>(*spec.memoryLimitBytes);
    const rlimit limit{limitValue, limitValue};
    if (prlimit(pid, RLIMIT_AS, &limit, nullptr) < 0 && errno != ESRCH) return osError("prlimit");
  }
#endif
  // macOS has no prlimit; memoryLimitBytes is intentionally ignored there.
  ProcessResult result;
  std::optional<Clock::time_point> exited;
  bool killed = false;
  int status = 0;
  auto drain = [&](Fd& fd, std::string& text, bool& truncated) -> bool {
    char buffer[16384];
    // Bound work per iteration so a continuous writer cannot starve the deadline.
    for (int reads = 0; reads < 64; ++reads) {
      const auto count = read(fd.value, buffer, sizeof(buffer));
      if (count > 0) appendOutput(text, truncated, buffer, static_cast<std::size_t>(count), spec.maxOutputBytes);
      else if (count == 0) { fd.reset(); return true; }
      else if (errno == EINTR) continue;
      else if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
      else return false;
    }
    return true;
  };
  while (!child.reaped || outRead.value >= 0 || errRead.value >= 0) {
    if (!child.reaped) {
      const auto waited = waitpid(pid, &status, WNOHANG);
      if (waited == pid) { child.reaped = true; exited = Clock::now(); }
      else if (waited < 0 && errno != EINTR) return osError("waitpid");
    }
    const auto now = Clock::now();
    if (!killed && now - start >= spec.timeout) {
      result.timedOut = true;
      child.killGroup();
      killed = true;
    }
    if (exited && now - *exited >= std::chrono::seconds(2)) {
      child.killGroup();
      // A descendant may have escaped the group. Do not wait forever for its fd.
      outRead.reset(); errRead.reset();
    }
    if (child.reaped && outRead.value < 0 && errRead.value < 0) break;
    pollfd fds[] = {{outRead.value, POLLIN, 0}, {errRead.value, POLLIN, 0}};
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(spec.timeout - (now - start)).count();
    const int waitMs = killed ? 20 : static_cast<int>(std::clamp<long long>(remaining, 0, 20));
    if (poll(fds, 2, waitMs) < 0) { if (errno == EINTR) continue; return osError("poll"); }
    if (outRead.value >= 0 && fds[0].revents && !drain(outRead, result.stdoutText, result.stdoutTruncated)) return osError("stdout read");
    if (errRead.value >= 0 && fds[1].revents && !drain(errRead, result.stderrText, result.stderrTruncated)) return osError("stderr read");
  }
  if (WIFEXITED(status)) result.exitCode = WEXITSTATUS(status);
  else if (WIFSIGNALED(status)) { result.signal = WTERMSIG(status); result.exitCode = 128 + *result.signal; }
  result.wall = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start);
  return result;
}
} // namespace distill::detail
#endif
