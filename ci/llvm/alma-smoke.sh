#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
# Mount just the moved executable and tests; no SDK/build/repository is visible.
docker run --rm -v "$PWD/isolated:/test" -w /test almalinux:8 bash -c '
  set -euo pipefail
  dnf install -y gcc-c++ coreutils
  timeout 120 ./distill-stub --self-check
  timeout 120 ./distill-stub /usr/bin/g++ /test/smoke.cpp -std=gnu++17
'
