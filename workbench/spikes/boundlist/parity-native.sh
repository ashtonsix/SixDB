#!/bin/bash
set -euo pipefail
case "$(uname -m)" in
 aarch64) profiles=neon; tune=neoverse-v2 ;;
 *) profiles="avx2 avx512"; tune="${BOUNDLIST_TUNE:-zen5}" ;;
esac
for profile in $profiles; do
 case "$profile" in
 neon) march=armv8-a+simd ;;
 avx2) march=x86-64-v3 ;;
 avx512) if [ "$tune" = zen5 ]; then march=znver5; else march=graniterapids; fi ;;
 esac
 dir="build/boundlist/$profile"
 cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0' \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" -DSIXDB_SPIKES=boundlist
 cmake --build "$dir" --target ikea_boundlist_check ikea_streampack_check ikea_streampack_boundaries_check \
  ikea_example_boundlist_ordinary ikea_example_boundlist_composition \
  ikea_example_streampack_ordinary ikea_example_streampack_composition \
  boundlist_reads boundlist_mutation boundlist_packing boundlist_material -j "$SIXDB_BUILD_JOBS"
 for target in ikea_boundlist_check ikea_streampack_check ikea_streampack_boundaries_check \
  ikea_example_boundlist_ordinary ikea_example_boundlist_composition \
  ikea_example_streampack_ordinary ikea_example_streampack_composition; do
  "$dir/ikea/$target" > "$SIXDB_RESULTS/$profile-$target-check.txt"
 done
 "$dir/workbench/spikes/boundlist/boundlist_material" > "$SIXDB_RESULTS/$profile-material-check.txt"
 for target in reads mutation packing; do
  exe="$dir/workbench/spikes/boundlist/boundlist_$target"
  python3 workbench/spikes/boundlist/qualification-audit.py "$exe" "$SIXDB_RESULTS/$profile-$target-assembly.txt"
  cp "$exe" "$SIXDB_RESULTS/$profile-$target-binary"
 done
 PYTHONPATH=workbench/tools python3 -c 'from worker_context import GroupContext; import sys; GroupContext.from_env().progress("measuring",case=sys.argv[1])' "$profile-stream-parity"
 exe="$dir/workbench/spikes/boundlist/boundlist_reads"
 taskset -c "$SIXDB_CPU" "$exe" 1 131072 5 > "$SIXDB_RESULTS/$profile-parity-read-hot.csv"
 BOUNDLIST_READ_LARGE_ONLY=1 /usr/bin/time -v taskset -c "$SIXDB_CPU" "$exe" 4096 1048576 5 \
  > "$SIXDB_RESULTS/$profile-parity-read-large.csv" 2> "$SIXDB_RESULTS/$profile-parity-read-large-memory.txt"
 for nodes in 8 4096; do
  taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_mutation" "$nodes" 32768 5 \
   > "$SIXDB_RESULTS/$profile-parity-mutation-p$nodes.csv"
 done
 taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_packing" 64 5 > "$SIXDB_RESULTS/$profile-parity-packing.csv"
 python3 ikea/test/headers.py "$dir" --module boundlist > "$SIXDB_RESULTS/$profile-boundlist-headers.txt"
 python3 ikea/test/headers.py "$dir" --module streampack > "$SIXDB_RESULTS/$profile-streampack-headers.txt"
 cp "$dir/compile_commands.json" "$SIXDB_RESULTS/$profile-compile-commands.json"
done
python3 - "$SIXDB_RESULTS" <<'PY'
import collections,csv,pathlib,sys
for f in pathlib.Path(sys.argv[1]).glob('*.csv'):
 cells=collections.defaultdict(list)
 for row in csv.DictReader(f.open()):
  key=tuple((k,v) for k,v in row.items() if k not in ('rep','ns','ns_per_block'))
  cells[key].append(int(row['rep']))
 assert cells and all(sorted(v)==list(range(5)) for v in cells.values()),f
PY
