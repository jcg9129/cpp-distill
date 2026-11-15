#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Disposable Linux runner only. SDK mounts are declared in matrix.py.
set -euo pipefail
if [[ ${GITHUB_ACTIONS:-} != true || ${RUNNER_OS:-} != Linux ]]; then
  echo 'Cleanup is only permitted on a disposable GitHub Linux runner' >&2
  exit 1
fi
df -h .
# Source + native Release libraries need space alongside the installed archive.
# These three host toolchains are mounted solely for this cleanup. manylinux
# supplies our GCC; Android, .NET and GHC are unused. No toolcache is removed.
free_kib=$(df -Pk . | awk 'NR==2 {print $4}')
if (( free_kib < 20 * 1024 * 1024 )); then
  shopt -s dotglob nullglob
  directories=(/opt/unused-android /opt/unused-dotnet /opt/unused-ghc)
  if [[ ${1:-} == --host ]]; then
    directories=(/usr/local/lib/android /usr/share/dotnet /opt/ghc)
  fi
  for directory in "${directories[@]}"; do
    if [[ -d $directory ]]; then
      rm -rf -- "${directory:?}"/*
    fi
  done
fi
df -h .
