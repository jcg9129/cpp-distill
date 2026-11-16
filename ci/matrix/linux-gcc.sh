#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Official multi-architecture manifests, identical to extensions.yml.
set -euo pipefail
status=0
for image in \
  gcc:14.2.0@sha256:b99b86a28812b1e6453a231a947dc43d76fe192788a12f344a9b568bf9f5d24c \
  gcc:15.2.0@sha256:3ae15afe768b06d0c0fe088d822ba5f8045c26630bdacc8d8e7713cf5d8e7289; do
  label=${image%%@*}
  label=${label//:/-}
  # Use the runner architecture, including on ubuntu-24.04-arm.
  docker run --rm -v "$PWD:/work" -w /work -e SDK_PLATFORM "$image" bash -c '
    set -euo pipefail
    apt-get update -qq
    apt-get install -y --no-install-recommends python3
    python3 ci/matrix/smoke.py --platform "$SDK_PLATFORM" --label "$1" --parse-only
  ' bash "$label" || status=1
  docker image rm "$image" || true
done
exit "$status"
