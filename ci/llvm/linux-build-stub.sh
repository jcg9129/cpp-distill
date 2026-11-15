#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
python3 ci/llvm/acquire.py --source "$SDK_SOURCE" --version "$LLVM_VERSION" \
  --rev "$SDK_REV" --platform "$SDK_PLATFORM"
python3 ci/llvm/build-stub.py --platform "$SDK_PLATFORM"
