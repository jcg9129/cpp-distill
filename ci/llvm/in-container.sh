#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Linux spike/audit builds use Docker so subsequent smoke steps run on Ubuntu.
set -euo pipefail
image=$1
shift
docker run --rm -v "$PWD:/work" -w /work \
  -e LC_ALL=C -e SDK_SOURCE -e LLVM_VERSION -e SDK_REV -e SDK_PLATFORM -e GITHUB_REPOSITORY \
  -e GITHUB_STEP_SUMMARY -e GITHUB_TOKEN -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
  "$image" bash -c '
    set -euo pipefail
    source ci/llvm/environment.sh
    python3 -m pip install --disable-pip-version-check cmake==3.31.6 ninja==1.13.0
    "$@"
  ' bash "$@"
