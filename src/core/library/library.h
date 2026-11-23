// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/profile/profile.h"
#include <cstdint>
#include <span>

namespace distill {
// Dense, interned snapshots. 0 = absent (identity), 1 = unknown, >=2 = value.
struct LibraryHeader {
  std::string_view selector, header;
  std::span<const std::uint16_t> states;
};
struct LibraryInput {
  std::string_view name, definition; // Empty definition requires source-entry absence.
};
struct LibraryBuild {
  // selector is empty for builds with no reproduced-profile snapshot.
  std::string_view selector, major, gcc, dialect, triple, build;
  std::span<const std::string_view> flags;
  // Standard macro agreement under this target, dialect and ordered argv.
  std::string_view standardAgreement{};
  std::span<const LibraryInput> standardInputs{};
};
struct LibraryData {
  std::span<const std::string_view> names, values;
  std::span<const LibraryHeader> headers;
  std::span<const LibraryBuild> builds;
  std::string_view provenance;
  // Sorted global union, independent of profile/feature-macro selection.
  std::span<const std::string_view> memberNames{};
  std::string_view memberInventoryProvenance{};
  // Generator 5 widens facts to target-sensitive standard macros. Agreement
  // requires the same ordered non-harmless argv, including absence of flags.
  // Older baselines contain feature macros only and retain their coverage rule.
  bool exactFlags = false;
  const LibraryHeader* find(std::string_view selector, std::string_view header) const;
  std::uint16_t state(const LibraryHeader&, std::string_view name) const;
  std::string selector(const Profile&, bool agreementOnly = false, std::string* reason = nullptr,
                       bool featureOnly = false) const;
  // Feature-only major agreement predates target-sensitive standard values.
  // Satisfaction may use it across targets, retaining scanned flag coverage.
  std::string featureAgreementSelector(const Profile&) const;
};
const LibraryData& embeddedLibraryData();
} // namespace distill
