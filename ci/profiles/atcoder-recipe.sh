# SPDX-License-Identifier: GPL-3.0-or-later
# Compiler stanza of AtCoder's published 017-23_gcc_14-2-0.toml install script,
# researched 2026-11-15 (display and VERSION are now GCC 15.2.0).
# Source: https://img.atcoder.jp/file/language-update/2025-10/017-23_gcc_14-2-0.toml
# Only prefix, temporary directories, sudo and parallelism differ from the recipe.
set -euo pipefail
prefix=$1
work=$2
mkdir -p "$prefix" "$work"
cd "$work"
curl --fail --location --retry 3 'http://ftp.tsukuba.wide.ad.jp/software/gcc/releases/gcc-15.2.0/gcc-15.2.0.tar.gz' -o gcc.tar.gz
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
