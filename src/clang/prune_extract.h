// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/prune/extract.h"
namespace distill {
// Only call inside the bounded pruning worker. Never initialize Clang in its parent.
Result<PruningModel> extractPruningInWorker(PruningModel);
} // namespace distill
