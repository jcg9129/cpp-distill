// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "check.h"
#include "core/path.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace test {
inline std::filesystem::path helper;
struct TempDirectory {
  std::filesystem::path path;
  TempDirectory() {
    static std::atomic<unsigned> sequence{0};
    for (int attempt = 0; attempt < 100; ++attempt) {
      auto candidate = std::filesystem::current_path() / (".distill-unit-" +
          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(sequence++));
      if (std::filesystem::create_directory(candidate)) { path = candidate; return; }
    }
    throw std::runtime_error("cannot create test directory");
  }
  ~TempDirectory() { std::error_code ec; std::filesystem::remove_all(path, ec); }
  void write(const std::filesystem::path& name, const std::string& contents) const {
    std::ofstream file(path / name, std::ios::binary);
    file << contents;
    if (!file) throw std::runtime_error("cannot write test fixture");
  }
};
inline std::string read(const std::filesystem::path& file) {
  std::ifstream input(file, std::ios::binary);
  if (!input) throw std::runtime_error("cannot read " + distill::pathToUtf8(file));
  return {(std::istreambuf_iterator<char>(input)), {}};
}
inline std::string fixture(const std::string& name) { return read(std::filesystem::path(DISTILL_FIXTURES) / name); }
} // namespace test
