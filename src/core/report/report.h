// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/pipeline/pipeline.h"
namespace distill {
std::string renderReport(const PipelineResult&, const CompileCommand&, const Profile*);
} // namespace distill
