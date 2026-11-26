// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/result.h"
#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace distill {
struct ProcessSpec {
  std::vector<std::string> argv;
  std::filesystem::path cwd;
  std::optional<std::filesystem::path> stdinFile;
  std::chrono::milliseconds timeout{30000};
  std::size_t maxOutputBytes = 4 * 1024 * 1024;
  std::optional<std::size_t> memoryLimitBytes;
};
struct ProcessResult {
  int exitCode = -1;
  std::optional<int> signal;
  bool timedOut = false;
  bool stdoutTruncated = false, stderrTruncated = false;
  std::string stdoutText, stderrText;
  std::chrono::milliseconds wall{0};
};
using ProcessRunner = std::function<Result<ProcessResult>(const ProcessSpec&)>;
// Explicit PATH lookup. Relative PATH entries and executable paths use cwd.
Result<std::filesystem::path> resolveExecutable(const std::filesystem::path& executable,
                                              const std::filesystem::path& cwd);
// OS-reported path of this process, independent of argv[0] and PATH.
Result<std::filesystem::path> runningExecutable();
Result<ProcessResult> runProcess(const ProcessSpec& spec);
// Independent jobs only; return slots retain input order. The runner must be
// safe to call concurrently. Each process keeps its own timeout/output limits.
std::vector<Result<ProcessResult>> runProcessBatch(const std::vector<ProcessSpec>&,
                                                  unsigned jobs, ProcessRunner = runProcess);
using ProcessBatchRunner = std::function<std::vector<Result<ProcessResult>>(const std::vector<ProcessSpec>&)>;
namespace detail {
Result<ProcessResult> runNative(const ProcessSpec& spec, const std::filesystem::path& executable);
inline void appendOutput(std::string& text, bool& truncated, const char* data,
                         std::size_t size, std::size_t cap) {
  const auto available = cap - text.size();
  text.append(data, size < available ? size : available);
  if (size > available) truncated = true;
}
} // namespace detail
} // namespace distill
