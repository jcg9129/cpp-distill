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
ctest --test-dir build-resolver --output-on-failure --timeout 120 -j1
