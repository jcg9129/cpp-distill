#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
# Same compiler/glibc baseline as the SDK and distribution spike. Avoid relying
# on the Ubuntu runner's libstdc++ ABI for these static SDK archives.
trap 'rm -rf build-resolver spike-work' EXIT
python3 ci/llvm/acquire.py --source release --version 23.1.5 --rev 1 --platform linux-x86_64
cmake -S . -B build-resolver -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
  -DDISTILL_LLVM_DIR="$PWD/spike-work/sdk/lib/cmake/llvm"
cmake --build build-resolver -j2
# Includes the thin CLI, shared pipeline cases through its real binary, and
# CLI help/option errors, and both pruning extraction model/case suites.
# All subprocess/test deadlines remain bounded.
if ctest --test-dir build-resolver --output-on-failure --timeout 120 -j1; then
  exit 0
else
  status=$?
  # Preserve the original failure even if the diagnostic rerun passes.
  ctest --test-dir build-resolver -R prune_extract --output-on-failure -V --timeout 120 -j1 || true
  exit "$status"
fi
