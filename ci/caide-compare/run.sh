#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

usage() {
  echo 'Usage: ci/caide-compare/run.sh <atl-dir> <caide-cmd> <distill> <out-dir> [shard-index shard-count]'
  echo 'Run verify, noverify, distill-nlm and distill-compact; shard indices are zero-based (default: 0 1).'
}
die() { echo "caide-compare: $*" >&2; exit 1; }
if [[ ${1:-} == -h || ${1:-} == --help ]]; then usage; exit 0; fi
[[ $# -eq 4 || $# -eq 6 ]] || { usage >&2; exit 1; }
for tool in realpath find sort g++ sed wc cat cmp date timeout mkdir rm; do
  command -v "$tool" > /dev/null || die "missing tool: $tool"
done
atl=$(realpath -e -- "$1")
caide=$(realpath -e -- "$2")
distill=$(realpath -e -- "$3")
[[ -d $atl/docs/examples && -d $atl/include ]] || die 'atl-dir must contain docs/examples and include'
[[ -x $caide ]] || die 'caide-cmd must be executable'
[[ -x $distill ]] || die 'distill must be executable'
shard=${5:-0}
shards=${6:-1}
[[ $shard =~ ^[0-9]+$ && $shards =~ ^[1-9][0-9]*$ ]] || die 'invalid shard indices'
shard=$((10#$shard))
shards=$((10#$shards))
(( shard < shards )) || die 'shard-index must be less than shard-count'
mkdir -p -- "$4"
out=$(realpath -e -- "$4")
summary="$out/summary.tsv"
printf 'example\tvariant\tcaide_exit\tdistill_exit\tfallback\tfallback_reason\tbytes\tcompile\trun_check\tseconds\n' > "$summary"
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

# Every variant gets its own invocation and independent sample check. Output
# failures are comparison data; missing tools and incomplete sweeps are errors.
for ((index=shard; index<${#examples[@]}; index+=shards)); do
  src=${examples[index]}
  base=${src%.cpp}
  name=${base#"$atl/docs/examples/"}
  name=${name//\//-}
  for variant in verify noverify distill-nlm distill-compact; do
    started=$(date +%s)
    directory="$out/examples/$name"
    mkdir -p -- "$directory"
    generated="$directory/$variant.cpp"
    binary="$directory/$variant.bin"
    log="$directory/$variant.log"
    report="$directory/$variant.rep"
    rm -f -- "$generated" "$binary"
    : > "$report"
    : > "$log"
    rc=0
    caide_exit=-
    distill_exit=-
    fallback=-
    fallback_reason=-
    if [[ $variant == distill-* ]]; then
      flags=(--strip-comments --no-line-markers)
      if [[ $variant == distill-compact ]]; then flags+=(--compact-whitespace); fi
      # Match the nlm/compact commands and limits in ci/atl-sweep/sweep.sh.
      (
        ulimit -v 2500000
        timeout --kill-after=5s 300 "$distill" "${flags[@]}" --judge cses --report "$report" -o "$generated" \
          g++ -std=c++23 -O2 -I"$atl/include" "$src"
      ) > "$log" 2>&1 || rc=$?
      distill_exit=$rc
    else
      caide_log="$directory/$variant.caide.log"
      options=(-c -o "$generated" --report "$report" --verify-with "$judge_command")
      if [[ -f $base.in ]]; then options+=(--verify-run "$base.in"); fi
      if [[ $variant == noverify ]]; then options+=(--no-verify); fi
      (
        ulimit -v 2500000
        timeout --kill-after=5s 600 "$caide" "${clang_flags[@]}" -- "${options[@]}" "$src"
      ) > "$caide_log" 2>&1 || rc=$?
      caide_exit=$rc
      cat "$caide_log" >> "$log"

      # Prefer the report's reason to the stderr warning. Inspect only caide
      # diagnostics, before adding independent compiler/runtime errors.
      fallback=none
      if [[ $variant == verify ]]; then
        fallback=unknown
        fallback_reason=$(sed -n 's/^# optimizer output was not used: fell back to unoptimized output: //p' "$report" | sed -n '1p')
        if [[ -z $fallback_reason ]]; then
          fallback_reason=$(sed -n 's/^caide inliner: warning: optimized output failed verification (\(.*\)); writing unoptimized output$/\1/p' "$caide_log" | sed -n '1p')
        fi
        if [[ -n $fallback_reason ]]; then
          fallback=yes
          fallback_reason=${fallback_reason//"$atl/"/}
          fallback_reason=${fallback_reason//$'\t'/ }
          fallback_reason=${fallback_reason//$'\r'/}
        elif [[ $rc -eq 0 && -s $generated ]]; then
          fallback=no
        fi
        fallback_reason=${fallback_reason:--}
      fi
    fi
    [[ $rc -ne 125 && $rc -ne 126 && $rc -ne 127 ]] || die "$variant could not run (exit $rc); see $log"

    compile=FAIL
    run_check=none
    if [[ -s $generated ]]; then
      compile_rc=0
      if (
        ulimit -v 2500000
        timeout --kill-after=5s 300 g++ -std=c++23 -O2 -o "$binary" "$generated"
      ) >> "$log" 2>&1; then
        compile=ok
      else
        compile_rc=$?
      fi
      [[ $compile_rc -ne 125 && $compile_rc -ne 126 && $compile_rc -ne 127 ]] || die "compiler could not run; see $log"
    else
      echo 'Missing generated source.' >> "$log"
    fi
    if [[ -f $base.out ]]; then
      run_check=FAIL
      input=/dev/null
      if [[ -f $base.in ]]; then input="$base.in"; fi
      if [[ $compile == ok ]]; then
        if (
          ulimit -v 2500000
          timeout --kill-after=5s 60 "$binary" < "$input"
        ) 2>> "$log" | cmp -s - "$base.out"; then
          run_check=ok
        else
          echo 'Sample execution failed or stdout differs from expected .out.' >> "$log"
        fi
      fi
    elif [[ $variant == distill-* && $rc -eq 0 ]]; then
      echo 'Missing expected .out file.' >> "$log"
      run_check=FAIL
    fi
    rm -f -- "$binary"
    bytes=0
    if [[ -f $generated ]]; then bytes=$(wc -c < "$generated"); fi
    seconds=$(($(date +%s) - started))
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
      "$name" "$variant" "$caide_exit" "$distill_exit" "$fallback" "$fallback_reason" "$bytes" "$compile" "$run_check" "$seconds" >> "$summary"
    printf '%s [%s]: exit=%s fallback=%s bytes=%s compile=%s run=%s seconds=%s\n' \
      "$name" "$variant" "$rc" "$fallback" "$bytes" "$compile" "$run_check" "$seconds"
  done
done
exit 0
