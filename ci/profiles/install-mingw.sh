# SPDX-License-Identifier: GPL-3.0-or-later
# Run in the preinstalled MSYS2 bash on windows-2025. Do not substitute versions.
set -euo pipefail
version=$1
[[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo 'Expected exact GCC major.minor.patch' >&2; exit 1; }
pacman -Sy --noconfirm
package=mingw-w64-x86_64-gcc
available=$(pacman -Si "$package" | sed -n 's/^Version[[:space:]]*:[[:space:]]*//p')
if [[ $available != "$version"-* ]]; then
  echo "Required $package $version is not available in the MSYS2 repository (available: $available). No replacement version will be installed. Restore a matching repository snapshot and rerun." >&2
  exit 1
fi
pacman -S --needed --noconfirm "$package=$available"
actual=$(/mingw64/bin/g++ -dumpfullversion -dumpversion)
[[ $actual == "$version" ]] || { echo "Installed GCC mismatch: expected $version, got $actual" >&2; exit 1; }
