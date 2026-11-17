# SPDX-License-Identifier: GPL-3.0-or-later
# CI only on the ubuntu-24.04 amd64 runner. No revision fallback.
set -euo pipefail
provenance=${1:?Pass the build identity, or snapshot_compiler when build is absent}
if [[ ! $provenance =~ ^package=ubuntu-24\.04:g\+\+-([1-9][0-9]*)=([0-9][A-Za-z0-9.+~]*-[A-Za-z0-9.+~]+)$ ]]; then
  echo "Unsupported Ubuntu compiler provenance: $provenance" >&2
  exit 1
fi
major=${BASH_REMATCH[1]}
revision=${BASH_REMATCH[2]}
# This file is provided by the explicitly selected CI runner image.
. /etc/os-release
[[ $ID == ubuntu && $VERSION_ID == 24.04 && $(dpkg --print-architecture) == amd64 ]] || {
  echo 'Exact Ubuntu snapshot requires Ubuntu 24.04 amd64' >&2; exit 1;
}
sudo apt-get update -qq
# Pin the GCC compiler/development package family together, so a newer runner
# image cannot prevent apt from downgrading its already-installed dependencies.
# Runtime packages remain the runner's versions; this is an approximate snapshot.
packages=("g++-$major" "gcc-$major" "cpp-$major" "gcc-$major-base"
          "g++-$major-x86-64-linux-gnu" "gcc-$major-x86-64-linux-gnu"
          "cpp-$major-x86-64-linux-gnu" "libstdc++-$major-dev" "libgcc-$major-dev")
exact=()
for package in "${packages[@]}"; do
  available=$(apt-cache madison "$package")
  if ! awk -F '|' -v revision="$revision" '{gsub(/^[ \t]+|[ \t]+$/, "", $2); if ($2 == revision) found=1} END {exit !found}' <<< "$available"; then
    echo "Exact Ubuntu package unavailable in configured archive: $package=$revision (no fallback)" >&2
    exit 1
  fi
  exact+=("$package=$revision")
done
sudo apt-get install -y --no-install-recommends --allow-downgrades "${exact[@]}"
for package in "${packages[@]}"; do
  actual=$(dpkg-query -W -f='${Version}' "$package")
  [[ $actual == "$revision" ]] || { echo "Installed package differs: $package=$actual; expected $revision" >&2; exit 1; }
done
# Metadata only: capture this actual spelling in the snapshot as well.
"g++-$major" -dumpfullversion -dumpversion
"g++-$major" -dumpmachine
