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
# CLI help/option/runtime checks, all 81 caide assertion cases (one CTest per
# case), and both pruning extraction model/case suites.
# All subprocess/test deadlines remain bounded.
status=0
if ctest --test-dir build-resolver --output-on-failure --timeout 120 -j1; then
  :
else
  status=$?
  # Preserve the original failure even if the diagnostic rerun passes.
  ctest --test-dir build-resolver -R 'caide_|prune_extract|pipeline|checker|cli_options' --output-on-failure -V --timeout 120 -j1 || true
fi
# Each solution gets its own 120-second runner deadline. Keep the observations
# outside build-resolver so the EXIT cleanup leaves workflow artifacts intact.
python3 ci/measure.py --runner build-resolver/tests/runner/distill_measure \
  --worker build-resolver/distill_prune_worker \
  --root tests/cases/prune --root tests/cases/pipeline --output measurements/adversarial || status=1
python3 ci/measure.py --runner build-resolver/tests/runner/distill_measure \
  --worker build-resolver/distill_prune_worker \
  --root tests/corpus/caide --output measurements/caide || status=1
exit "$status"
