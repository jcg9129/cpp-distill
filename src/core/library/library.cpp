// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/library/library.h"
#include <algorithm>

namespace distill {
namespace {
bool harmlessFlag(std::string_view flag) {
  // -Wp, forwards arbitrary preprocessor options, including -D and -U.
  // The last optimization level requires a scanned match: it alters
  // __OPTIMIZE__, __OPTIMIZE_SIZE__ and __NO_INLINE__. Earlier levels are
  // discarded by selector's relevant-flags comparison, not treated as harmless.
  return (flag.starts_with("-W") && !flag.starts_with("-Wp,")) ||
         flag.starts_with("-g") || flag == "-pipe" || flag.starts_with("-fdiagnostics-") ||
         flag.starts_with("-fconstexpr-depth=") || flag.starts_with("-fconstexpr-cache-depth=") ||
         flag.starts_with("-fconstexpr-loop-limit=") || flag.starts_with("-fconstexpr-ops-limit=");
}
bool optimizationLevel(std::string_view flag) {
  if (!flag.starts_with("-O")) return false;
  const auto level = flag.substr(2);
  return level.empty() || level == "s" || level == "fast" || level == "g" || level == "z" ||
         std::all_of(level.begin(), level.end(), [](char c) { return c >= '0' && c <= '9'; });
}
bool compilerInputsMatch(const LibraryBuild& build, const Profile& p, std::string* reason) {
  for (const auto& input : build.standardInputs) {
    const auto fact = p.snapshotFacts.find(input.name);
    const bool absent = fact == p.snapshotFacts.end() || fact->second.state == MacroState::undefined;
    bool agrees = input.definition.empty() && absent;
    if (!input.definition.empty() && fact != p.snapshotFacts.end()) {
      const auto expected = parseMacros(input.definition);
      agrees = expected.ok() && expected.value().size() == 1 &&
          fact->second.state == MacroState::known && fact->second.definition &&
          fact->second.provenance == std::vector{FactOrigin::compilerBuiltin} &&
          expected.value().begin()->first == input.name &&
          expected.value().begin()->second == *fact->second.definition;
    }
    if (!agrees) {
      if (reason) *reason = "profile compiler input does not match captured library agreement: " + std::string(input.name);
      return false;
    }
  }
  return true;
}
}
const LibraryHeader* LibraryData::find(std::string_view selector, std::string_view header) const {
  const auto key = std::pair(selector, header);
  auto it = std::lower_bound(headers.begin(), headers.end(), key, [](const auto& row, const auto& k) {
    return std::pair(row.selector, row.header) < k;
  });
  if (it != headers.end() && it->selector == selector && it->header == header) return &*it;
  const auto scope = selector.find('@');
  return scope != std::string_view::npos ? find(selector.substr(0, scope), header) : nullptr;
}
std::uint16_t LibraryData::state(const LibraryHeader& h, std::string_view name) const {
  auto it = std::lower_bound(names.begin(), names.end(), name);
  if (it == names.end() || *it != name) return 1;
  return h.states[it - names.begin()];
}
std::string LibraryData::featureAgreementSelector(const Profile& p) const {
  return selector(p, true, nullptr, true);
}
std::string LibraryData::selector(const Profile& p, bool agreementOnly, std::string* reason, bool featureOnly) const {
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
    const auto relevant = [](const auto& flags) {
      // Keep aligned with ci/library/merge.py. GCC uses the last -O level;
      // explicit -f options still matter regardless of their position.
      std::vector<std::string_view> result;
      bool seenOptimization = false;
      for (auto it = flags.rbegin(); it != flags.rend(); ++it) {
        if (optimizationLevel(*it)) {
          if (seenOptimization) continue;
          seenOptimization = true;
        }
        if (!harmlessFlag(*it)) result.emplace_back(*it);
      }
      std::reverse(result.begin(), result.end());
      return result;
    };
    const auto flags = relevant(p.flags);
    if (exactFlags && !featureOnly) {
      const auto target = [&](const auto& b) {
        return covered(b) && compatibleProfileTriple(b.triple, p.triple);
      };
      if (std::none_of(builds.begin(), builds.end(), target)) {
        if (reason) *reason = "profile target not covered by scanned builds: " + p.triple;
        return {};
      }
      std::string inputReason;
      const auto match = std::find_if(builds.begin(), builds.end(), [&](const auto& b) {
            return target(b) && relevant(b.flags) == flags && compilerInputsMatch(b, p, &inputReason);
          });
      if (match != builds.end()) {
        return match->standardAgreement.empty() ? "gcc-" + major : std::string(match->standardAgreement);
      }
      if (!inputReason.empty()) {
        if (reason) *reason = std::move(inputReason);
        return {};
      }
      if (reason) {
        *reason = "profile ordered flags not matched by a scanned build:";
        for (const auto flag : flags) *reason += " " + std::string(flag);
      }
      return {};
    }
    if (std::any_of(builds.begin(), builds.end(), [&](const auto& b) {
          if (!covered(b)) return false;
          const auto buildFlags = relevant(b.flags);
          return std::all_of(flags.begin(), flags.end(), [&](const auto& flag) {
            return std::find(buildFlags.begin(), buildFlags.end(), flag) != buildFlags.end();
          });
        })) return "gcc-" + major;
    std::string required, uncovered;
    for (const auto flag : flags) {
      if (!required.empty()) required += ' ';
      required += flag;
      if (std::any_of(builds.begin(), builds.end(), [&](const auto& b) {
            if (!covered(b)) return false;
            const auto buildFlags = relevant(b.flags);
            return std::find(buildFlags.begin(), buildFlags.end(), flag) != buildFlags.end();
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
        b.flags.size() != p.flags.size() || !std::equal(b.flags.begin(), b.flags.end(), p.flags.begin()) ||
        !compilerInputsMatch(b, p, reason)) return {};
    return key;
  }
  return {};
}
} // namespace distill
