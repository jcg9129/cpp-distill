// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/inline/inline.h"
#include "core/path.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace distill {
namespace {
std::filesystem::path key(const std::filesystem::path& cwd, const std::filesystem::path& p) {
  std::error_code ec;
  auto result = std::filesystem::weakly_canonical(cwd / p, ec);
  return ec ? (cwd / p).lexically_normal() : result;
}
bool same(const std::filesystem::path& cwd, const std::filesystem::path& a, const std::filesystem::path& b) {
  std::error_code ec;
  if (std::filesystem::equivalent(cwd / a, cwd / b, ec) && !ec) return true;
  return key(cwd, a) == key(cwd, b);
}
bool beneath(const std::filesystem::path& cwd, const std::filesystem::path& file, const std::filesystem::path& dir) {
  const auto f = key(cwd, file), d = key(cwd, dir);
  auto a = f.begin(), b = d.begin();
  for (; b != d.end(); ++b, ++a) {
    if (a == f.end()) return false;
#ifdef _WIN32
    if (CompareStringOrdinal(a->c_str(), -1, b->c_str(), -1, TRUE) != CSTR_EQUAL) return false;
#else
    if (*a != *b) return false;
#endif
  }
  return true;
}
bool regularFile(const std::filesystem::path& p) {
  std::error_code ec; return std::filesystem::is_regular_file(p, ec) && !ec;
}
// filesystem::operator/ preserves dot components but can alter slash spelling.
std::string join(std::string_view dir, std::string_view name) {
  if (dir.empty()) return std::string(name);
  const bool separator = dir.back() == '/'
#ifdef _WIN32
      || dir.back() == '\\'
#endif
      ;
  return std::string(dir) + (separator ? "" : "/") + std::string(name);
}
}
Result<IncludeSearch> buildIncludeSearch(const CompileCommand& cmd, const SearchLists& probe) {
  try {
    IncludeSearch result{cmd.cwd, {}, {}};
    std::vector<IncludeDirectory> systems;
    auto append = [&](auto& list, IncludeDirectory dir) {
      if (std::none_of(list.begin(), list.end(), [&](const auto& old) { return same(cmd.cwd, dir.path, old.path); }))
        list.push_back(std::move(dir));
    };
    for (const auto& dir : cmd.systemIncludePaths()) append(systems, {dir.path, true, false});
    for (const auto& dir : probe.directories)
      if (dir.kind != IncludeKind::quote) append(systems, {dir.path, true, dir.framework});
    for (const auto& dir : cmd.userIncludePaths()) {
      if (dir.kind == IncludeKind::quote) { append(result.quote, {dir.path, false, false}); continue; }
      if (std::none_of(systems.begin(), systems.end(), [&](const auto& sys) { return same(cmd.cwd, dir.path, sys.path); }))
        append(result.angle, {dir.path, false, false});
    }
    for (auto& dir : systems) append(result.angle, std::move(dir));
    return result;
  } catch (const std::exception& e) { return Error{std::string("include search: ") + e.what()}; }
}
Result<HeaderLookup> lookupHeader(const IncludeSearch& search, std::string_view name, bool angle, std::string_view includer) {
  try {
    auto candidate = [&](std::string spelling, bool system, std::optional<std::filesystem::path> directory) {
      HeaderLookup hit{search.cwd / pathFromUtf8(spelling), spelling, false, system, std::move(directory)};
      hit.found = regularFile(hit.path); return hit;
    };
    if (pathFromUtf8(name).is_absolute()) {
      auto hit = candidate(std::string(name), false, {});
      for (const auto& dir : search.angle) if (dir.system && beneath(search.cwd, hit.path, dir.path)) {
        hit.system = true; hit.systemDirectory = dir.path; break;
      }
      return hit;
    }
    if (!angle) {
      // GCC omits './' for main.cpp, but retains it for ./main.cpp and -I ./lib.
      const auto slash = includer.find_last_of(
#ifdef _WIN32
          "/\\"
#else
          "/"
#endif
      );
      const auto directory = slash == includer.npos ? std::string_view{} : includer.substr(0, slash + 1);
      auto hit = candidate(join(directory, name), false, {});
      if (hit.found) return hit;
      for (const auto& dir : search.quote) {
        hit = candidate(join(pathToUtf8(dir.path), name), false, {});
        if (hit.found) return hit;
      }
    }
    for (const auto& dir : search.angle) {
      auto spelling = join(pathToUtf8(dir.path), name);
      if (dir.framework) {
        const auto slash = name.find('/');
        if (slash == name.npos) continue;
        spelling = join(pathToUtf8(dir.path), std::string(name.substr(0, slash)) + ".framework/Headers/" + std::string(name.substr(slash + 1)));
      }
      auto hit = candidate(std::move(spelling), dir.system, dir.system ? std::optional(dir.path) : std::nullopt);
      if (hit.found) return hit;
    }
    return HeaderLookup{};
  } catch (const std::exception& e) { return Error{std::string("header lookup: ") + e.what()}; }
}
Result<FileIdentity> fileIdentity(const std::filesystem::path& path) {
  try {
#ifdef _WIN32
  const auto handle = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return Error{"cannot open file identity: " + pathToUtf8(path)};
  BY_HANDLE_FILE_INFORMATION info{};
  const bool ok = GetFileInformationByHandle(handle, &info) != 0;
  CloseHandle(handle);
  if (!ok) return Error{"cannot get file identity: " + pathToUtf8(path)};
  return FileIdentity{info.dwVolumeSerialNumber, (std::uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow};
#else
  struct stat info{};
  if (stat(path.c_str(), &info) != 0) return Error{"cannot stat: " + pathToUtf8(path)};
  return FileIdentity{static_cast<std::uint64_t>(info.st_dev), static_cast<std::uint64_t>(info.st_ino)};
#endif
  } catch (const std::exception& e) { return Error{std::string("file identity: ") + e.what()}; }
}
std::string onceGuardName(FileIdentity id, std::uint64_t salt) {
  // FNV-1a over fixed little-endian integers, independent of host endianness.
  std::uint64_t hash = 14695981039346656037ULL;
  for (auto value : {id.device, id.file, salt}) for (unsigned b = 0; b < 8; ++b) {
    hash ^= (value >> (8 * b)) & 255; hash *= 1099511628211ULL;
  }
  std::ostringstream out; out << "DISTILL_ONCE_" << std::hex << std::setfill('0') << std::setw(16) << hash;
  return out.str();
}
} // namespace distill
