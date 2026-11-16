// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/prune/prune.h"
namespace distill {
// Analysis only; this does not certify or select a pruned artifact (D3b).
Result<std::string> renderPruningReport(const PruningModel&, const PruningResult&);
}
