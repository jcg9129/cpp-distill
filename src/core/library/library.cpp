// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/library/library.h"
#include <algorithm>

namespace distill {
namespace {
bool harmlessFlag(std::string_view flag) {
  // -Wp, forwards arbitrary preprocessor options, including -D and -U.
  // Optimization levels deliberately require a scanned match: they alter
  // __OPTIMIZE__, __OPTIMIZE_SIZE__ and __NO_INLINE__.
  return (flag.starts_with("-W") && !flag.starts_with("-Wp,")) ||
         flag.starts_with("-g") || flag == "-pipe" || flag.starts_with("-fdiagnostics-") ||
         flag.starts_with("-fconstexpr-depth=") || flag.starts_with("-fconstexpr-cache-depth=") ||
         flag.starts_with("-fconstexpr-loop-limit=") || flag.starts_with("-fconstexpr-ops-limit=");
}
}
const LibraryHeader* LibraryData::find(std::string_view selector, std::string_view header) const {
  const auto key = std::pair(selector, header);
  auto it = std::lower_bound(headers.begin(), headers.end(), key, [](const auto& row, const auto& k) {
    return std::pair(row.selector, row.header) < k;
  });
  return it != headers.end() && it->selector == selector && it->header == header ? &*it : nullptr;
}
std::uint16_t LibraryData::state(const LibraryHeader& h, std::string_view name) const {
  auto it = std::lower_bound(names.begin(), names.end(), name);
  if (it == names.end() || *it != name) return 1;
  return h.states[it - names.begin()];
}
std::string LibraryData::featureAgreementSelector(const Profile& p) const {
  auto features = *this;
  features.exactFlags = false;
  return features.selector(p, true);
}
std::string LibraryData::selector(const Profile& p, bool agreementOnly, std::string* reason) const {
  if (reason) reason->clear();
  if (p.compiler.family != CompilerFamily::gcc ||
      std::find(p.libraries.begin(), p.libraries.end(), "libstdc++") == p.libraries.end()) return {};
  if (!p.reproduced || agreementOnly) {
    const auto major = std::to_string(p.compiler.major);
    const auto covered = [&](const LibraryBuild& b) { return b.major == major && b.dialect == p.dialect.name; };
    if (std::none_of(builds.begin(), builds.end(), covered)) {
      if (reason) *reason = "profile dialect not covered by scanned builds: " + p.dialect.name;
      return {};
    }
    if (exactFlags) {
      const auto target = [&](const auto& b) {
        return covered(b) && compatibleProfileTriple(b.triple, p.triple);
      };
      if (std::none_of(builds.begin(), builds.end(), target)) {
        if (reason) *reason = "profile target not covered by scanned builds: " + p.triple;
        return {};
      }
      const auto relevant = [](const auto& flags) {
        std::vector<std::string_view> result;
        for (const auto& flag : flags) if (!harmlessFlag(flag)) result.emplace_back(flag);
        return result;
      };
      const auto flags = relevant(p.flags);
      if (std::any_of(builds.begin(), builds.end(), [&](const auto& b) {
            return target(b) && relevant(b.flags) == flags;
          })) return "gcc-" + major;
      if (reason) {
        *reason = "profile ordered flags not matched by a scanned build:";
        for (const auto flag : flags) *reason += " " + std::string(flag);
      }
      return {};
    }
    if (std::any_of(builds.begin(), builds.end(), [&](const auto& b) {
          return covered(b) && std::all_of(p.flags.begin(), p.flags.end(), [&](const auto& flag) {
            return harmlessFlag(flag) || std::find(b.flags.begin(), b.flags.end(), flag) != b.flags.end();
          });
        })) return "gcc-" + major;
    std::string required, uncovered;
    for (const auto& flag : p.flags) {
      if (harmlessFlag(flag)) continue;
      if (!required.empty()) required += ' ';
      required += flag;
      if (std::any_of(builds.begin(), builds.end(), [&](const auto& b) {
            return covered(b) && std::find(b.flags.begin(), b.flags.end(), flag) != b.flags.end();
          })) continue;
      if (!uncovered.empty()) uncovered += ' ';
      uncovered += flag;
    }
    if (reason) *reason = !uncovered.empty()
        ? "profile flags not covered by scanned builds: " + uncovered
        : "profile flag combination not covered by a single scanned build: " + required;
    return {};
  }
  const auto key = "profile-" + p.name;
  // A renamed/edited user profile must not borrow another build's snapshots.
  for (const auto& b : builds) if (b.selector == key) {
    if (b.gcc != p.snapshotMetadata.gccVersion || b.dialect != p.dialect.name ||
        !compatibleProfileTriple(b.triple, p.triple) || b.build != p.snapshotMetadata.build ||
        b.flags.size() != p.flags.size() || !std::equal(b.flags.begin(), b.flags.end(), p.flags.begin())) return {};
    return key;
  }
  return {};
}
} // namespace distill
