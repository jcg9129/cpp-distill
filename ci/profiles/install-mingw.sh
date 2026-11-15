# SPDX-License-Identifier: GPL-3.0-or-later
# CI only, in the preinstalled MSYS2 bash on windows-2025. No version fallback.
set -euo pipefail
[[ $1 == 14.2.0 ]] || { echo 'Only researched Codeforces GCC 14.2.0 is supported' >&2; exit 1; }
archive=https://repo.msys2.org/mingw/mingw64
packages="$PWD/out/mingw-packages"
mkdir -p "$packages"
files=()
while IFS= read -r file; do
  [[ -z $file || $file == \#* ]] && continue
  [[ $file =~ ^mingw-w64-x86_64-[a-zA-Z0-9.+_-]+-any\.pkg\.tar\.zst$ ]] || exit 1
  if ! curl --fail --location --retry 3 "$archive/$file" -o "$packages/$file"; then
    echo "Exact archived package unavailable: $archive/$file. No replacement is permitted." >&2
    exit 1
  fi
  files+=("$packages/$file")
done < ci/profiles/mingw-packages.txt
# One transaction permits the matching gcc/gcc-libs downgrade together.
# No -Sy or -S: dependencies must be satisfied by this exact set or fail.
pacman -U --noconfirm "${files[@]}"
[[ $(pacman -Q mingw-w64-x86_64-gcc) == 'mingw-w64-x86_64-gcc 14.2.0-1' ]]
[[ $(pacman -Q mingw-w64-x86_64-gcc-libs) == 'mingw-w64-x86_64-gcc-libs 14.2.0-1' ]]
[[ $(/mingw64/bin/g++ -dumpfullversion -dumpversion) == 14.2.0 ]]
