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
  cmake --build "$dir" --target boundlist_interpolation boundlist_ranges boundlist_promotion boundlist_cold boundlist_cold_compact boundlist_prediction_check ikea_boundlist_check -j "$SIXDB_BUILD_JOBS"
  "$dir/ikea/ikea_boundlist_check" > "$SIXDB_RESULTS/$profile-production-check.txt"
  "$dir/workbench/spikes/boundlist/boundlist_prediction_check" > "$SIXDB_RESULTS/$profile-prediction-check.txt"
  for target in interpolation ranges promotion cold cold_compact; do
    exe="$dir/workbench/spikes/boundlist/boundlist_$target"
    python3 workbench/spikes/boundlist/qualification-audit.py "$exe" "$SIXDB_RESULTS/$profile-$target-assembly.txt"
    cp "$exe" "$SIXDB_RESULTS/$profile-$target-binary"
  done
  PYTHONPATH=workbench/tools python3 -c 'from worker_context import GroupContext; import sys; GroupContext.from_env().progress("measuring",case=sys.argv[1])' "$profile-production-binding"
  for bits in 16 32 64; do
    for n in 256 4096 16384; do
      for distribution in 0 1 2; do
        taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_interpolation" "$bits" "$n" "$distribution" 131072 5 \
          > "$SIXDB_RESULTS/$profile-final-k$bits-n$n-d$distribution.csv" \
          2> "$SIXDB_RESULTS/$profile-final-k$bits-n$n-d$distribution-preparation.txt"
      done
    done
  done
  for bits in 16 32 64; do
    for n in 256 4096; do
      for nodes in 1 64; do
        for repeat in 1 8; do
          for sparse in 0 1; do
            for point in 0 1; do
              taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_ranges" "$bits" "$nodes" "$n" "$repeat" 1 257 "$sparse" "$point" 65536 3 \
                > "$SIXDB_RESULTS/$profile-final-range-k$bits-n$n-p$nodes-d$repeat-s$sparse-point$point.csv"
            done
          done
        done
      done
    done
  done
  for bits in 32 64; do
    taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_ranges" "$bits" 1 16384 8 1 257 1 0 65536 3 \
      > "$SIXDB_RESULTS/$profile-final-range-k$bits-n16384.csv"
  done
  PYTHONPATH=workbench/tools python3 -c 'from worker_context import GroupContext; import sys; GroupContext.from_env().progress("measuring",case=sys.argv[1])' "$profile-checkpoint-frames"
  for nodes in 1 524288; do
    if [ "$nodes" = 1 ]; then ops=131072; else ops=2097152; fi
    for mixed in 0 1; do
      for point in 0 1; do
        for variant in cold cold_compact; do
          BOUNDLIST_COLD_PRODUCTION_ONLY=1 /usr/bin/time -v taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_$variant" "$nodes" "$mixed" 257 "$point" "$ops" 3 \
            > "$SIXDB_RESULTS/$profile-final-$variant-p$nodes-m$mixed-point$point.csv" \
            2> "$SIXDB_RESULTS/$profile-final-$variant-p$nodes-m$mixed-point$point-memory.txt"
        done
      done
    done
  done
  for nodes in 1 64 4096; do
    for maximum in 255 4095; do
      for point in 0 1; do
        taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_promotion" "$nodes" "$maximum" "$point" 131072 3 \
          > "$SIXDB_RESULTS/$profile-final-promotion-p$nodes-w$maximum-point$point.csv"
      done
    done
  done
  python3 ikea/test/headers.py "$dir" --module boundlist > "$SIXDB_RESULTS/$profile-headers.txt"
  cp "$dir/compile_commands.json" "$SIXDB_RESULTS/$profile-compile-commands.json"
done
python3 - "$SIXDB_RESULTS" <<'PY'
import collections,csv,pathlib,sys
for f in pathlib.Path(sys.argv[1]).glob('*.csv'):
    cells=collections.defaultdict(list)
    for row in csv.DictReader(f.open()):
        key=tuple((k,v) for k,v in row.items() if k not in ('rep','ns','setup_ns','close_ns'))
        cells[key].append(int(row['rep']))
    expected=list(range(5 if '-final-k' in f.name else 3))
    assert cells and all(sorted(v)==expected for v in cells.values()),f
PY
