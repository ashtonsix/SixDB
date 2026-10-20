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
  cmake --build "$dir" --target boundlist_interpolation boundlist_prediction_check -j "$SIXDB_BUILD_JOBS"
  "$dir/workbench/spikes/boundlist/boundlist_prediction_check" > "$SIXDB_RESULTS/$profile-prediction-check.txt"
  exe="$dir/workbench/spikes/boundlist/boundlist_interpolation"
  python3 workbench/spikes/boundlist/qualification-audit.py "$exe" "$SIXDB_RESULTS/$profile-interpolation-assembly.txt"
  cp "$exe" "$SIXDB_RESULTS/$profile-interpolation-binary"
  PYTHONPATH=workbench/tools python3 -c 'from worker_context import GroupContext; import sys; GroupContext.from_env().progress("measuring",case=sys.argv[1])' "$profile"
  for bits in 16 32 64; do
    for n in 64 256 1024 4096 16384; do
      for distribution in 0 1 2; do
        taskset -c "$SIXDB_CPU" "$exe" "$bits" "$n" "$distribution" 131072 5 \
          > "$SIXDB_RESULTS/$profile-adaptive-k$bits-n$n-d$distribution.csv" \
          2> "$SIXDB_RESULTS/$profile-adaptive-k$bits-n$n-d$distribution-preparation.txt"
      done
    done
  done
  cp "$dir/compile_commands.json" "$SIXDB_RESULTS/$profile-compile-commands.json"
done
python3 - "$SIXDB_RESULTS" <<'PY'
import collections,csv,pathlib,sys
for f in pathlib.Path(sys.argv[1]).glob('*.csv'):
    cells=collections.defaultdict(list)
    for row in csv.DictReader(f.open()):
        key=tuple((k,v) for k,v in row.items() if k not in ('rep','ns'))
        cells[key].append(int(row['rep']))
    assert cells and all(sorted(v)==[0,1,2,3,4] for v in cells.values()),f
PY
