#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

usage() {
  echo 'Usage: ci/caide-compare/run.sh <atl-dir> <caide-cmd> <out-dir> [shard-index shard-count]'
  echo 'Run verify and noverify variants; shard indices are zero-based (default: 0 1).'
}
die() { echo "caide-compare: $*" >&2; exit 1; }
if [[ ${1:-} == -h || ${1:-} == --help ]]; then usage; exit 0; fi
[[ $# -eq 3 || $# -eq 5 ]] || { usage >&2; exit 1; }
atl=$(realpath -e -- "$1")
caide=$(realpath -e -- "$2")
[[ -d $atl/docs/examples && -d $atl/include ]] || die 'atl-dir must contain docs/examples and include'
[[ -x $caide ]] || die 'caide-cmd must be executable'
shard=${4:-0}
shards=${5:-1}
[[ $shard =~ ^[0-9]+$ && $shards =~ ^[1-9][0-9]*$ ]] || die 'invalid shard indices'
shard=$((10#$shard))
shards=$((10#$shards))
(( shard < shards )) || die 'shard-index must be less than shard-count'
mkdir -p -- "$3"
out=$(realpath -e -- "$3")
summary="$out/summary.tsv"
printf 'example\tvariant\tcaide_exit\tfallback\tbytes\tcompile\trun_check\tseconds\n' > "$summary"
: > "$out/examples.txt"

# Keep discovery, names, compiler flags and sample comparisons aligned with
# ci/atl-sweep/sweep.sh. In particular, that sweep does not define NDEBUG.
mapfile -d '' -t examples < <(find "$atl/docs/examples" -type f -name '*.cpp' -print0 | LC_ALL=C sort -z)
[[ ${#examples[@]} -gt 0 ]] || die 'no examples found'
for ((index=shard; index<${#examples[@]}; index+=shards)); do
  name=${examples[index]#"$atl/docs/examples/"}
  name=${name%.cpp}
  printf '%s\n' "${name//\//-}" >> "$out/examples.txt"
done
printf 'Discovered %s examples; shard %s/%s selects %s.\n' \
  "${#examples[@]}" "$shard" "$shards" "$(wc -l < "$out/examples.txt")"

# Supply the runner's actual libstdc++ search paths to embedded Clang, without
# assuming a particular GCC release or architecture-specific include directory.
g++ -std=c++23 -E -x c++ -v /dev/null > /dev/null 2> "$out/compiler-search.log"
clang_flags=(-std=c++23 -O2 -I"$atl/include")
while IFS= read -r include; do
  [[ -d $include ]] && clang_flags+=(-isystem "$include")
done < <(sed -n '/#include <...> search starts here:/,/End of search list./{s/^ //p;}' "$out/compiler-search.log")
judge_command='g++ -std=c++23 -O2'

# One shared 600-second budget for BOTH variants and their independent checks.
# A timed-out first variant still gets a row, as does the unattempted second.
# The grace period only lets timeout reap children; compilation and execution
# retain the sweep's 300/60-second limits inside this shared budget.
remaining() {
  local limit=$1 left
  left=$((deadline - $(date +%s)))
  (( left > 0 )) || return 1
  if (( left < limit )); then limit=$left; fi
  printf '%s\n' "$limit"
}
failed=0
for ((index=shard; index<${#examples[@]}; index+=shards)); do
  src=${examples[index]}
  base=${src%.cpp}
  name=${base#"$atl/docs/examples/"}
  name=${name//\//-}
  deadline=$(($(date +%s) + 600))
  for variant in verify noverify; do
    started=$(date +%s)
    mkdir -p -- "$out/$variant"
    generated="$out/$variant/$name.cpp"
    binary="$out/$variant/$name.bin"
    log="$out/$variant/$name.log"
    caide_log="$out/$variant/$name.caide.log"
    report="$out/$variant/$name.rep"
    rm -f -- "$generated" "$binary"
    : > "$report"
    : > "$log"
    options=(-c -o "$generated" --report "$report" --verify-with "$judge_command")
    if [[ -f $base.in ]]; then options+=(--verify-run "$base.in"); fi
    if [[ $variant == noverify ]]; then options+=(--no-verify); fi
    rc=0
    if budget=$(remaining 600); then
      (
        ulimit -v 2500000
        timeout --kill-after=5s "$budget" "$caide" "${clang_flags[@]}" -- "${options[@]}" "$src"
      ) > "$caide_log" 2>&1 || rc=$?
    else
      rc=124
      echo 'Shared 600-second example budget exhausted.' > "$caide_log"
    fi
    cat "$caide_log" >> "$log"

    # CI archives src/cmd and src/inliner.cpp to make this classification
    # auditable against the selected fork revision. Check only caide diagnostics,
    # before adding independent compiler/runtime errors. A failed/incomplete
    # invocation without an explicit fallback message remains unknown.
    fallback=none
    if [[ $variant == verify ]]; then
      fallback=unknown
      if grep -Eiq 'fall(ing)?[[:space:]-]+back|fallback([[:space:]]+to|[=:][[:space:]]*(true|yes|1))|using[[:space:]]+(the[[:space:]]+)?(unoptimi[sz]ed|raw|unpruned)' "$caide_log" "$report"; then
        fallback=yes
      elif [[ $rc -eq 0 && -s $generated ]]; then
        fallback=no
      fi
    fi

    compile=FAIL
    run_check=none
    if [[ -s $generated ]] && budget=$(remaining 300); then
      if (
        ulimit -v 2500000
        timeout --kill-after=5s "$budget" g++ -std=c++23 -O2 -o "$binary" "$generated"
      ) >> "$log" 2>&1; then
        compile=ok
      fi
    else
      echo 'Missing generated source or example budget exhausted before compile.' >> "$log"
    fi
    if [[ -f $base.out ]]; then
      run_check=FAIL
      input=/dev/null
      if [[ -f $base.in ]]; then input="$base.in"; fi
      if [[ $compile == ok ]] && budget=$(remaining 60); then
        if (
          ulimit -v 2500000
          timeout --kill-after=5s "$budget" "$binary" < "$input"
        ) 2>> "$log" | cmp -s - "$base.out"; then
          run_check=ok
        else
          echo 'Sample execution failed or stdout differs from expected .out.' >> "$log"
        fi
      fi
    fi
    rm -f -- "$binary"
    bytes=0
    if [[ -f $generated ]]; then bytes=$(wc -c < "$generated"); fi
    seconds=$(($(date +%s) - started))
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
      "$name" "$variant" "$rc" "$fallback" "$bytes" "$compile" "$run_check" "$seconds" >> "$summary"
    printf '%s [%s]: caide=%s fallback=%s bytes=%s compile=%s run=%s seconds=%s\n' \
      "$name" "$variant" "$rc" "$fallback" "$bytes" "$compile" "$run_check" "$seconds"
    if [[ $rc -ne 0 || $compile == FAIL || $run_check == FAIL ]]; then failed=1; fi
  done
done
exit "$failed"
