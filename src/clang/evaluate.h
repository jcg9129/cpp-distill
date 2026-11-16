// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/resolve/resolve.h"

namespace distill {
class ClangEvaluator final : public ConditionEvaluator {
public:
  Result<bool> evaluate(const ConditionInput&) override;
};
} // namespace distill
