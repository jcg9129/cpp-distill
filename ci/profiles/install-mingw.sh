# SPDX-License-Identifier: GPL-3.0-or-later
# CI only, in the preinstalled MSYS2 bash on windows-2025. No version fallback.
set -euo pipefail
provenance=${1:?Pass the build identity, or snapshot_compiler when build is absent}
packages="$PWD/out/mingw-packages"
mkdir -p "$packages"
# Resolve .PKGINFO depend/provides against the exact lock and live listing.
# Keep output in a file so resolver failure cannot be lost in process substitution.
python ci/profiles/mingw.py "$provenance" > "$packages/resolved.txt"
files=()
while IFS= read -r file; do
  files+=("$packages/$file")
done < "$packages/resolved.txt"
[[ ${#files[@]} -gt 0 ]] || { echo 'No resolved MSYS2 packages' >&2; exit 1; }
# One transaction permits the matching gcc/gcc-libs downgrade together.
# No -Sy or -S: dependencies must be satisfied by this exact set or fail.
pacman -U --noconfirm "${files[@]}"
# Verify every installed revision, not just GCC's upstream version.
for file in "${files[@]}"; do
  name=$(tar -xOf "$file" .PKGINFO | sed -n 's/^pkgname = //p')
  version=$(tar -xOf "$file" .PKGINFO | sed -n 's/^pkgver = //p')
  [[ $(pacman -Q "$name") == "$name $version" ]] || { echo "Installed package differs: $name $version" >&2; exit 1; }
done
version=${provenance##*=}
[[ $(/mingw64/bin/g++ -dumpfullversion -dumpversion) == "${version%-*}" ]]
