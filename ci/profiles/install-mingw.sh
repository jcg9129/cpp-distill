# SPDX-License-Identifier: GPL-3.0-or-later
# CI only, in the preinstalled MSYS2 bash on windows-2025. No version fallback.
set -euo pipefail
provenance=${1:?Pass the build identity, or snapshot_compiler when build is absent}
# MSYS2's login shell resets PATH. Use actions/setup-python explicitly, and
# supply every MSYS2 tool that the native Windows interpreter will launch.
python_dir=$(/usr/bin/cygpath -u "${pythonLocation:?actions/setup-python is required}")
py="$python_dir/python.exe"
MSYS2_TAR=$(/usr/bin/cygpath -w /usr/bin/tar.exe)
MSYS2_ZSTD=$(/usr/bin/cygpath -w /usr/bin/zstd.exe)
MSYS2_CURL=$(/usr/bin/cygpath -w /usr/bin/curl.exe)
MSYS2_VERCMP=$(/usr/bin/cygpath -w /usr/bin/vercmp.exe)
export MSYS2_TAR MSYS2_ZSTD MSYS2_CURL MSYS2_VERCMP
packages="$PWD/out/mingw-packages"
/usr/bin/mkdir -p "$packages"
# Resolve .PKGINFO depend/provides against the exact lock and live listing.
# Keep output in a file so resolver failure cannot be lost in process substitution.
# Convert all filesystem arguments explicitly and disable automatic argv
# conversion for this native process. Python prints filenames, not full paths.
resolver=$(/usr/bin/cygpath -w "$PWD/ci/profiles/mingw.py")
lock=$(/usr/bin/cygpath -w "$PWD/ci/profiles/mingw-packages.txt")
package_directory=$(/usr/bin/cygpath -w "$packages")
MSYS2_ARG_CONV_EXCL='*' "$py" "$resolver" "$provenance" \
  --lock "$lock" --directory "$package_directory" > "$packages/resolved.txt"
files=()
while IFS= read -r file; do
  file=${file%$'\r'} # Native Python writes CRLF; bash read only removes LF.
  files+=("$packages/$file")
done < "$packages/resolved.txt"
[[ ${#files[@]} -gt 0 ]] || { echo 'No resolved MSYS2 packages' >&2; exit 1; }
# One transaction permits the matching gcc/gcc-libs downgrade together.
# No -Sy or -S: dependencies must be satisfied by this exact set or fail.
/usr/bin/pacman -U --noconfirm "${files[@]}"
# Verify every installed revision, not just GCC's upstream version.
for file in "${files[@]}"; do
  name=$(/usr/bin/tar --use-compress-program=/usr/bin/zstd -xOf "$file" .PKGINFO | /usr/bin/sed -n 's/^pkgname = //p')
  version=$(/usr/bin/tar --use-compress-program=/usr/bin/zstd -xOf "$file" .PKGINFO | /usr/bin/sed -n 's/^pkgver = //p')
  [[ $(/usr/bin/pacman -Q "$name") == "$name $version" ]] || { echo "Installed package differs: $name $version" >&2; exit 1; }
done
version=${provenance##*=}
actual_version=$(/mingw64/bin/g++ -dumpfullversion -dumpversion)
actual_version=${actual_version%$'\r'}
[[ $actual_version == "${version%-*}" ]]
