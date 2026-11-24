// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/prune/prune.h"
namespace distill {
using RenameMap = std::map<std::string,std::string,std::less<>>;
struct ShortenedSource {
  std::string text;
  RenameMap renames;
  std::string namesKept; // Renaming refused; the compact artifact is kept.
};
// Name/position observations in raw user tokens or expanded user macros.
bool shorteningObservesNames(std::string_view);
Result<ShortenedSource> shortenNamesSource(std::string_view compact, const PruningModel&);
// Independent scan of the actual candidate, including directive membership,
// object/function macro definition adjacency and literal bytes.
Result<bool> shortenNamesInvariant(std::string_view compact, std::string_view candidate, const RenameMap&);
} // namespace distill
