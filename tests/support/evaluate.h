// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/resolve/resolve.h"
#include <memory>

namespace distill::testing {
class GccEvaluator final : public ConditionEvaluator {
  std::filesystem::path compiler_, root_;
  std::optional<IntegerModel> localModel_;
public:
  GccEvaluator(std::filesystem::path compiler, std::filesystem::path temporaryRoot)
      : compiler_(std::move(compiler)), root_(std::move(temporaryRoot)) {}
  Result<bool> evaluate(const ConditionInput&) override;
};
// Select gcc (default), clang or differential through DISTILL_TEST_EVALUATOR.
// Differential evaluates every submitted synthetic input with both engines.
std::unique_ptr<ConditionEvaluator> testEvaluator(const std::filesystem::path& root);
std::optional<std::filesystem::path> testGcc();
} // namespace distill::testing
