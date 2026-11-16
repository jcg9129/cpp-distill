#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
# The compatibility matrix reuses this minimum-platform entry point. Only the
# moved binaries, copied fixtures/harness and generated evidence are mounted.
if [[ ${1:-} == --cli ]]; then
  docker run --rm -v "$PWD/isolated:/test" -w /test almalinux:8 bash -c '
    set -euo pipefail
    dnf install -y gcc-c++ python3.11
    python3.11 smoke.py --binary ./distill --fixtures fixtures --output evidence \
      --platform linux-x86_64 --label almalinux8 --compiler /usr/bin/g++ \
      --std gnu++17 --minimum --smoke-only
  '
  exit 0
fi
# Mount just the moved executable and tests; no SDK/build/repository is visible.
docker run --rm -v "$PWD/isolated:/test" -w /test almalinux:8 bash -c '
  set -euo pipefail
  dnf install -y gcc-c++  # timeout comes from the preinstalled coreutils-single
  timeout 120 ./distill-stub --self-check
  timeout 120 ./distill-stub /usr/bin/g++ /test/smoke.cpp -std=gnu++17
'
