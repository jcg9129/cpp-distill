#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
# Exercise every installed GCC major, with at least GCC 14 or 15 present.
selected=()
reference=0
for candidate in /opt/homebrew/bin/g++-[0-9]* /usr/local/bin/g++-[0-9]*; do
  [[ -x $candidate ]] || continue
  selected+=("$candidate")
  if [[ $candidate == *g++-14 || $candidate == *g++-15 ]]; then reference=1; fi
done
if (( reference == 0 )); then
  brew install gcc@15
  # Versioned formulae can be keg-only: don't depend on a global symlink.
  compiler="$(brew --prefix gcc@15)/bin/g++-15"
  [[ -x $compiler ]] || { echo 'Homebrew GCC 15 not found' >&2; exit 1; }
  selected+=("$compiler")
fi
status=0
for compiler in "${selected[@]}"; do
  python3 ci/matrix/smoke.py --platform "$SDK_PLATFORM" --compiler "$compiler" \
    --label "homebrew-${compiler##*/}" --apple-default "$@" || status=1
done
exit "$status"
