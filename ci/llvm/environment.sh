#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Source this in Linux container steps. Do not use the manylinux Python 3.6.
set -euo pipefail
if [[ $(uname -s) == Linux ]]; then
  export PATH="/opt/python/cp312-cp312/bin:$PATH"
  # manylinux's supported GCC toolset, including its static C++ runtime.
  mapfile -t toolsets < <(compgen -G '/opt/rh/gcc-toolset-*/enable' | sort -V)
  if (( ${#toolsets[@]} == 0 )); then
    echo 'No manylinux gcc-toolset found' >&2
    return 1
  fi
  set +u
  # shellcheck disable=SC1090
  source "${toolsets[-1]}"
  set -u
fi
