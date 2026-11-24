// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/prune/prune.h"
namespace distill {
using RenameMap = std::map<std::string,std::string,std::less<>>;
struct MinifiedSource {
  std::string text;
  RenameMap renames;
  std::string namesKept; // T2 refused; T1 still applies.
};
Result<MinifiedSource> minifySource(std::string_view compact, const PruningModel&);
// Independent scan of the actual candidate, including directive membership,
// object/function macro definition adjacency and literal bytes.
Result<bool> minifyInvariant(std::string_view compact, std::string_view candidate, const RenameMap&);
} // namespace distill
