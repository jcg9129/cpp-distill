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
    std::string uncovered;
    for (const auto& flag : p.flags) {
      if (harmlessFlag(flag) || std::any_of(builds.begin(), builds.end(), [&](const auto& b) {
            return covered(b) && std::find(b.flags.begin(), b.flags.end(), flag) != b.flags.end();
          })) continue;
      if (!uncovered.empty()) uncovered += ' ';
      uncovered += flag;
    }
    if (!uncovered.empty()) {
      if (reason) *reason = "profile flags not covered by scanned builds: " + uncovered;
      return {};
    }
    return "gcc-" + major;
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
