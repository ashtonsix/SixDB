#!/bin/bash
# Software-clock samples attribute CPU work, not cache misses or unprofiled ns.
# Keep preparation/oracle/coordinator samples visible in the complete report.
set -euo pipefail
root=$1
mkdir -p "$root/profiles"
if ! command -v perf > "$root/profiles/perf-path.txt"; then
  echo 'perf unavailable; no dynamic attribution' > "$root/profiles/unavailable.txt"
  exit 0
fi
perf --version > "$root/profiles/version.txt"
for spec in 40_0_0 40_1_2 40_2_2; do
  for suffix in '' _packet1 _packet2; do
    name=hashtable_design_worker_epochs_${spec}_policy5${suffix}
    exe=$root/bin/$name
    # An extended steady mutation trace makes the callback observable instead
    # of relying on profiles dominated by thread creation and first promotion.
    for n in 256 1024; do
      stem=$root/profiles/$name-n$n
      if sudo -n perf record -e cpu-clock:u -F 1999 --call-graph dwarf,16384 \
          -o "$stem.data" -- "$exe" bench 1 "$n" 4096 > "$stem.csv" 2> "$stem.record.txt"; then
        sudo -n perf report --stdio --no-children -i "$stem.data" > "$stem.report.txt" 2> "$stem.report-errors.txt"
        sudo -n perf report --stdio --no-children --sort comm,dso,symbol -i "$stem.data" \
          > "$stem.symbols.txt" 2>> "$stem.report-errors.txt"
        sudo -n perf report --stdio --no-children --sort symbol,srcline -i "$stem.data" \
          > "$stem.lines.txt" 2>> "$stem.report-errors.txt"
      else
        echo 'Software-clock sampling refused; retain the diagnostic, no hotspot claim' > "$stem.unavailable.txt"
      fi
    done
  done
done
