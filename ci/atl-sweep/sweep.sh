#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

usage() {
  echo 'Usage: ci/atl-sweep/sweep.sh <atl-dir> <distill> <out-dir> <judge>'
  echo 'Measure plain, strip, nlm and compact variants; write summary.tsv, reports and logs.'
}

die() {
  echo "atl-sweep: $*" >&2
  exit 1
}

if [[ ${1:-} == -h || ${1:-} == --help ]]; then
  usage
  exit 0
fi
[[ $# -eq 4 ]] || { usage >&2; exit 1; }
atl=$(realpath -e -- "$1")
distill=$(realpath -e -- "$2")
[[ -d $atl/docs/examples && -d $atl/include ]] || die 'atl-dir must contain docs/examples and include'
[[ -x $distill ]] || die 'distill must be executable'
mkdir -p -- "$3"
out=$(realpath -e -- "$3")
judge=$4
summary="$out/summary.tsv"
printf 'example\tvariant\tdistill_exit\tartifact\trun_check\tbytes\tline_markers\tpruning_disabled\n' > "$summary"

mapfile -d '' -t examples < <(find "$atl/docs/examples" -type f -name '*.cpp' -print0 | LC_ALL=C sort -z)
[[ ${#examples[@]} -gt 0 ]] || die 'no examples found'
failed=0
for src in "${examples[@]}"; do
  base=${src%.cpp}
  name=${base#"$atl/docs/examples/"}
  name=${name//\//-}
  for variant in plain strip nlm compact; do
    flags=()
    case "$variant" in
      strip) flags=(--strip-comments) ;;
      nlm) flags=(--strip-comments --no-line-markers) ;;
      compact) flags=(--strip-comments --no-line-markers --compact-whitespace) ;;
    esac
    mkdir -p -- "$out/$variant"
    report="$out/$variant/$name.rep"
    generated="$out/$variant/$name.cpp"
    binary="$out/$variant/$name.bin"
    log="$out/$variant/$name.log"
    # A failed invocation must not reuse an output from an earlier sweep.
    rm -f -- "$generated" "$binary"
    : > "$report"
    rc=0
    (
      ulimit -v 2500000
      timeout 300 "$distill" "${flags[@]}" --judge "$judge" --report "$report" -o "$generated" \
        g++ -std=c++23 -O2 -I"$atl/include" "$src"
    ) > "$log" 2>&1 || rc=$?

    # The verification mode in parentheses is not part of the artifact kind.
    kind=$(sed -n '1s/^distill: wrote \(pruned\|flat\|raw\) .*/\1/p' "$report")
    kind=${kind:--}
    reason=''
    if [[ $kind != pruned ]]; then
      reason=$(grep -m1 'pruning disabled' "$report" || true)
      reason=${reason//"$atl/"/}
      reason=${reason//$'\t'/ }
      reason=${reason//$'\r'/}
    fi

    run_check=-
    if [[ -s $generated && -f $base.out ]]; then
      input=/dev/null
      if [[ -f $base.in ]]; then input="$base.in"; fi
      # Compile the flattened source on its own, without the atl include path.
      if (
        ulimit -v 2500000
        timeout 300 g++ -std=c++23 -O2 -o "$binary" "$generated"
      ) >> "$log" 2>&1 && (
        ulimit -v 2500000
        timeout 60 "$binary" < "$input"
      ) 2>> "$log" | cmp -s - "$base.out"; then
        run_check=ok
      else
        run_check=FAIL
      fi
    elif [[ $rc -eq 0 ]]; then
      echo 'Missing generated source or expected .out file.' >> "$log"
      run_check=FAIL
    fi
    rm -f -- "$binary"

    bytes=0
    line_markers=0
    if [[ -f $generated ]]; then
      bytes=$(wc -c < "$generated")
      line_markers=$(grep -c '^#line' "$generated" || true)
    fi
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
      "$name" "$variant" "$rc" "$kind" "$run_check" "$bytes" "$line_markers" "$reason" >> "$summary"
    printf '%s [%s]: distill=%s artifact=%s run=%s bytes=%s\n' "$name" "$variant" "$rc" "$kind" "$run_check" "$bytes"
    if [[ $rc -ne 0 || $run_check == FAIL ]]; then failed=1; fi
  done
done

python3 - "$summary" "$judge" <<'PY'
import collections
import csv
import os
import statistics
import sys

with open(sys.argv[1], encoding='utf-8', newline='') as stream:
    rows = list(csv.DictReader(stream, delimiter='\t'))

lines = [
    f'### atl-cpp sweep ({sys.argv[2]})',
    '',
    '| Variant | Examples | Median bytes | Max bytes | < 65536 bytes | Pruned | Run FAILs |',
    '| --- | ---: | ---: | ---: | ---: | ---: | ---: |',
]
for variant in ('plain', 'strip', 'nlm', 'compact'):
    group = [row for row in rows if row['variant'] == variant]
    sizes = [int(row['bytes']) for row in group]
    lines.append(
        f'| {variant} | {len(group)} | {statistics.median(sizes)} | {max(sizes)} | '
        f'{sum(size < 65536 for size in sizes)} | '
        f'{sum(row["artifact"] == "pruned" for row in group)} | '
        f'{sum(row["run_check"] == "FAIL" for row in group)} |'
    )

failures = sum(int(row['distill_exit']) != 0 for row in rows)
lines.extend(['', f'Distill non-zero exits: {failures}.', '', 'Pruning-disabled reasons (example/variant rows):', ''])
reasons = collections.Counter(row['pruning_disabled'] for row in rows if row['pruning_disabled'])
if reasons:
    lines.extend(['| Count | First pruning-disabled reason |', '| ---: | --- |'])
    for reason, count in sorted(reasons.items(), key=lambda item: (-item[1], item[0])):
        escaped = reason.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;').replace('|', '&#124;')
        lines.append(f'| {count} | {escaped} |')
else:
    lines.append('None.')
markdown = '\n'.join(lines) + '\n'
print(markdown, end='')
if os.environ.get('GITHUB_STEP_SUMMARY'):
    with open(os.environ['GITHUB_STEP_SUMMARY'], 'a', encoding='utf-8') as stream:
        stream.write(markdown)
PY

exit "$failed"
