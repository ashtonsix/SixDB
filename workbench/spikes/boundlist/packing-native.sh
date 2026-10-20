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
 cmake --build "$dir" --target boundlist_packing -j "$SIXDB_BUILD_JOBS"
 exe="$dir/workbench/spikes/boundlist/boundlist_packing"
 python3 workbench/spikes/boundlist/qualification-audit.py "$exe" "$SIXDB_RESULTS/$profile-packing-assembly.txt"
 cp "$exe" "$SIXDB_RESULTS/$profile-packing-binary"
 taskset -c "$SIXDB_CPU" "$exe" 256 7 > "$SIXDB_RESULTS/$profile-parity-packing.csv"
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
 assert cells and all(sorted(v)==list(range(7)) for v in cells.values()),f
PY
