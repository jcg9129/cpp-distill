#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

usage() {
  echo "Usage: ci/measure-local.sh [--jobs N] [--keep-build] <corpus-dir>..."
}

die() {
  echo "measure-local: $*" >&2
  exit 1
}

repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
jobs=2
keep_build=0
roots=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --jobs)
      [[ $# -ge 2 ]] || die "--jobs requires a positive integer"
      [[ $2 =~ ^[1-9][0-9]*$ ]] || die "--jobs requires a positive integer"
      jobs=$2
      shift 2
      ;;
    --keep-build)
      keep_build=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    -*) die "unknown option: $1" ;;
    *) break ;;
  esac
done

[[ $# -gt 0 ]] || { usage >&2; exit 1; }
for corpus in "$@"; do
  [[ -d "$corpus" ]] || die "not a corpus directory: $corpus"
  # Resolve symlinks before checking containment; works with macOS bash 3.2.
  corpus=$(cd -- "$corpus" && pwd -P)
  case "$corpus" in
    "$repo_dir"/tests/corpus/user|"$repo_dir"/tests/corpus/user/*) ;;
    "$repo_dir"|"$repo_dir"/*)
      die "corpus must be outside the work tree (or under tests/corpus/user/): $corpus"
      ;;
  esac
  roots+=(--root "$corpus")
done

case "$(uname -s):$(uname -m)" in
  Linux:x86_64) platform=linux-x86_64 ;;
  Linux:aarch64|Linux:arm64) platform=linux-arm64 ;;
  Darwin:arm64|Darwin:aarch64) platform=macos-arm64 ;;
  Darwin:x86_64) platform=macos-x86_64 ;;
  *) die "unsupported platform (requires Linux or macOS on x86_64 or arm64)" ;;
esac
for tool in python3 cmake ninja; do
  command -v "$tool" >/dev/null || die "required tool not found: $tool"
done
# measure.py calls 'timeout'; Homebrew coreutils calls it 'gtimeout'.
timeout_command=$(command -v timeout || command -v gtimeout || true)
[[ -n "$timeout_command" ]] || die "GNU timeout is required (on macOS: brew install coreutils)"

cd -- "$repo_dir"
build_dir="$repo_dir/build-measure"
[[ ! -L "$build_dir" ]] || die "build-measure must not be a symlink"
cleanup() {
  if [[ $keep_build -eq 0 ]]; then
    rm -rf -- "$build_dir"
  fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

sdk_dir="$repo_dir/spike-work/sdk"
# Keep the unpacked SDK in the existing ignored cache, independently of build
# cleanup. acquire.py itself removes compressed downloads after extraction.
if [[ ! -f "$sdk_dir/lib/cmake/llvm/LLVMConfig.cmake" ]]; then
  [[ ! -e "$sdk_dir" ]] || die "incomplete SDK at $sdk_dir; repair it before retrying"
  export GITHUB_REPOSITORY="${GITHUB_REPOSITORY:-jcg9129/cpp-distill}"
  python3 ci/llvm/acquire.py --source release --version 23.1.5 --rev 1 --platform "$platform"
fi

configure_args=(-DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
  -DDISTILL_PROFILE_ONLY=OFF "-DDISTILL_LLVM_DIR=$sdk_dir/lib/cmake/llvm")
case "$platform" in
  linux-*) configure_args+=(-DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++) ;;
  macos-*) configure_args+=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
    "-DCMAKE_OSX_ARCHITECTURES=${platform#macos-}" -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0) ;;
esac
cmake -S . -B "$build_dir" -G Ninja "${configure_args[@]}"
cmake --build "$build_dir" --target distill_measure distill_prune_worker -j "$jobs"

mkdir -p "$build_dir/bin"
ln -sf "$timeout_command" "$build_dir/bin/timeout"
export PATH="$build_dir/bin:$PATH"
python3 ci/measure.py --runner "$build_dir/tests/runner/distill_measure" \
  --worker "$build_dir/distill_prune_worker" "${roots[@]}" \
  --output measurements/local/user
