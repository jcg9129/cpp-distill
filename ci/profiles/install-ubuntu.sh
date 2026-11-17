# SPDX-License-Identifier: GPL-3.0-or-later
# CI only on the Ubuntu amd64 release in the identity. No revision fallback.
set -euo pipefail
provenance=${1:?Pass the build identity, or snapshot_compiler when build is absent}
if [[ ! $provenance =~ ^package=ubuntu-([0-9]{2}\.[0-9]{2}):g\+\+-([1-9][0-9]*)=([0-9][A-Za-z0-9.+~]*-[A-Za-z0-9.+~]+)$ ]]; then
  echo "Unsupported Ubuntu compiler provenance: $provenance" >&2
  exit 1
fi
release=${BASH_REMATCH[1]}
major=${BASH_REMATCH[2]}
revision=${BASH_REMATCH[3]}
# This file is provided by the explicitly selected CI runner/container image.
. /etc/os-release
[[ $ID == ubuntu && $VERSION_ID == "$release" && $(dpkg --print-architecture) == amd64 ]] || {
  echo "Exact Ubuntu snapshot requires Ubuntu $release amd64" >&2; exit 1;
}
# Official Ubuntu job containers run as root and do not include sudo.
apt_command=(apt-get)
if (( EUID != 0 )); then apt_command=(sudo apt-get); fi
export DEBIAN_FRONTEND=noninteractive
"${apt_command[@]}" update -qq
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
"${apt_command[@]}" install -y --no-install-recommends --allow-downgrades "${exact[@]}"
for package in "${packages[@]}"; do
  actual=$(dpkg-query -W -f='${Version}' "$package")
  [[ $actual == "$revision" ]] || { echo "Installed package differs: $package=$actual; expected $revision" >&2; exit 1; }
done
# Metadata only: capture this actual spelling in the snapshot as well.
"g++-$major" -dumpfullversion -dumpversion
"g++-$major" -dumpmachine
