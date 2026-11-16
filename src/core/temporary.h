// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/result.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace distill {
struct TemporaryDirectory {
  std::filesystem::path path;
  bool keep = false;
  explicit TemporaryDirectory(std::string_view prefix,
                              const std::optional<std::filesystem::path>& overrideRoot = {}) {
    static std::atomic<unsigned> sequence{0};
    const auto root = std::filesystem::absolute(overrideRoot ? *overrideRoot : std::filesystem::temp_directory_path());
    for (int attempt = 0; attempt < 100; ++attempt) {
      const auto candidate = root / (std::string(prefix) +
          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(sequence++));
      if (!std::filesystem::create_directory(candidate)) continue;
      std::error_code ec;
      std::filesystem::permissions(candidate, std::filesystem::perms::owner_all, ec);
      if (ec) {
        std::filesystem::remove(candidate);
        throw Error{"cannot make temporary directory private: " + ec.message()};
      }
      path = candidate;
      return;
    }
    throw Error{"cannot create temporary directory"};
  }
  TemporaryDirectory(const TemporaryDirectory&) = delete;
  TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
  ~TemporaryDirectory() {
    if (!keep) { std::error_code ec; std::filesystem::remove_all(path, ec); }
  }
};
} // namespace distill
