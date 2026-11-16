#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
mode=${1:?build, build-release, isolate, isolate-release, or smoke}
case "$mode" in
  build) python3 ci/matrix/build.py --platform "$SDK_PLATFORM" ;;
  build-release) python3 ci/matrix/build.py --platform "$SDK_PLATFORM" --release ;;
  isolate)
    python3 ci/matrix/build.py --platform "$SDK_PLATFORM" --isolate
    chown -R "$HOST_UID:$HOST_GID" isolated results
    ;;
  isolate-release)
    python3 ci/matrix/build.py --platform "$SDK_PLATFORM" --isolate --release
    chown -R "$HOST_UID:$HOST_GID" isolated results
    ;;
  smoke)
    # environment.sh selected the same manylinux GCC toolset as the SDK.
    python3 ci/matrix/smoke.py --platform "$SDK_PLATFORM" --label manylinux --compiler "$(command -v g++)"
    ;;
  *) echo "Unknown matrix phase: $mode" >&2; exit 1 ;;
esac
