# SPDX-License-Identifier: GPL-3.0-or-later
# Compiler stanza of AtCoder's published 017-23_gcc_14-2-0.toml install script,
# researched 2026-11-15 (display and VERSION are now GCC 15.2.0).
# Source: https://img.atcoder.jp/file/language-update/2025-10/017-23_gcc_14-2-0.toml
# Only prefix, temporary directories, sudo and parallelism differ from the recipe.
set -euo pipefail
source_digest=$(cut -d " " -f 1 "$(dirname "$0")/atcoder-source.sha512")
prefix=$1
work=$2
mkdir -p "$prefix" "$work"
cd "$work"
curl --fail --location --retry 3 'https://ftp.gnu.org/gnu/gcc/gcc-15.2.0/gcc-15.2.0.tar.gz' -o gcc.tar.gz
echo "$source_digest  gcc.tar.gz" | sha512sum --check
mkdir -p gcc
tar -xzf gcc.tar.gz -C gcc --strip-components=1
cd gcc
./contrib/download_prerequisites
mkdir -p build
cd build
../configure CC=gcc CXX=g++ \
  --prefix="$prefix" \
  --enable-languages=c++ \
  --disable-bootstrap \
  --disable-multilib \
  --disable-libsanitizer \
  --disable-checking \
  --disable-nls \
  --disable-gcov \
  --disable-libada \
  --disable-libgm2
make -j"$(nproc)"
make install
