#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
mode=${1:?build, isolate, or smoke}
case "$mode" in
  build) python3 ci/matrix/build.py --platform "$SDK_PLATFORM" ;;
  isolate)
    python3 ci/matrix/build.py --platform "$SDK_PLATFORM" --isolate
    chown -R "$HOST_UID:$HOST_GID" isolated results
    ;;
  smoke)
    # environment.sh selected the same manylinux GCC toolset as the SDK.
    python3 ci/matrix/smoke.py --platform "$SDK_PLATFORM" --label manylinux --compiler "$(command -v g++)"
    ;;
  *) echo "Unknown matrix phase: $mode" >&2; exit 1 ;;
esac
