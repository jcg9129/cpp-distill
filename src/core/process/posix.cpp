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
struct SpawnSetup {
  posix_spawn_file_actions_t actions{};
  posix_spawnattr_t attr{};
  bool actionsReady = false, attrReady = false;
  ~SpawnSetup() {
    if (actionsReady) posix_spawn_file_actions_destroy(&actions);
    if (attrReady) posix_spawnattr_destroy(&attr);
  }
};
struct Child {
  pid_t pid;
  bool reaped = false;
  ~Child() {
    // Also contains descendants after a successful direct-child exit.
    kill(-pid, SIGKILL);
    if (!reaped) { int status; while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {} }
  }
};
} // namespace
Result<ProcessResult> runNative(const ProcessSpec& spec, const std::filesystem::path& executable) {
  const auto start = Clock::now();
  Fd outRead, outWrite, errRead, errWrite;
  if (!makePipe(outRead, outWrite) || !makePipe(errRead, errWrite)) return osError("pipe");
  if (fcntl(outRead.value, F_SETFL, O_NONBLOCK) < 0 || fcntl(errRead.value, F_SETFL, O_NONBLOCK) < 0)
    return osError("nonblocking pipe");
  SpawnSetup setup;
  int rc = posix_spawn_file_actions_init(&setup.actions);
  if (rc) return osError("spawn actions", rc);
  setup.actionsReady = true;
  rc = posix_spawnattr_init(&setup.attr);
  if (rc) return osError("spawn attributes", rc);
  setup.attrReady = true;
  std::error_code ec;
  auto cwd = std::filesystem::absolute(spec.cwd, ec);
  if (ec) return Error{"cwd: " + ec.message()};
  const auto stdinPath = spec.stdinFile ? cwd / *spec.stdinFile : std::filesystem::path("/dev/null");
  if ((rc = posix_spawn_file_actions_addchdir_np(&setup.actions, cwd.c_str())) ||
      (rc = posix_spawn_file_actions_addopen(&setup.actions, STDIN_FILENO, stdinPath.c_str(), O_RDONLY, 0)) ||
      (rc = posix_spawn_file_actions_adddup2(&setup.actions, outWrite.value, STDOUT_FILENO)) ||
      (rc = posix_spawn_file_actions_adddup2(&setup.actions, errWrite.value, STDERR_FILENO)) ||
      (rc = posix_spawnattr_setflags(&setup.attr, POSIX_SPAWN_SETPGROUP)) ||
      (rc = posix_spawnattr_setpgroup(&setup.attr, 0))) return osError("spawn setup", rc);
  std::vector<char*> argv;
  for (const auto& arg : spec.argv) argv.push_back(const_cast<char*>(arg.c_str()));
  argv.push_back(nullptr);
  pid_t pid;
  rc = posix_spawn(&pid, executable.c_str(), &setup.actions, &setup.attr, argv.data(), environ);
  if (rc) return osError("posix_spawn", rc);
  Child child{pid};
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
      kill(-pid, SIGKILL);
      killed = true;
    }
    if (exited && now - *exited >= std::chrono::seconds(2)) {
      kill(-pid, SIGKILL);
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
