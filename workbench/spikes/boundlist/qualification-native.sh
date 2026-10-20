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
  cmake --build "$dir" --target boundlist_check boundlist_material boundlist_reads boundlist_ranges boundlist_cold -j "$SIXDB_BUILD_JOBS"
  taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_check" > "$SIXDB_RESULTS/$profile-check.txt"
  taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_material" > "$SIXDB_RESULTS/$profile-material-check.txt"
  for target in reads ranges cold; do
    exe="$dir/workbench/spikes/boundlist/boundlist_$target"
    python3 workbench/spikes/boundlist/qualification-audit.py "$exe" "$SIXDB_RESULTS/$profile-$target-assembly.txt"
    cp "$exe" "$SIXDB_RESULTS/$profile-$target-binary"
  done
  for nodes in 1 64 4096; do
    taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_reads" "$nodes" 131072 5 \
      > "$SIXDB_RESULTS/$profile-reads-p$nodes.csv"
  done
  for bits in 16 32 64 128; do
    for repeat in 1 8 64; do
      for sparse in 0 1; do
        for point in 0 1; do
          taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_ranges" "$bits" 1 256 "$repeat" 1 257 "$sparse" "$point" 65536 5 \
            > "$SIXDB_RESULTS/$profile-ranges-k$bits-n256-p1-d$repeat-s$sparse-point$point.csv"
        done
      done
    done
    for point in 0 1; do
      taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_ranges" "$bits" 1024 256 8 1 257 1 "$point" 65536 5 \
        > "$SIXDB_RESULTS/$profile-ranges-k$bits-n256-p1024-point$point.csv"
      taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_ranges" "$bits" 64 4096 1 1 257 1 "$point" 32768 5 \
        > "$SIXDB_RESULTS/$profile-ranges-k$bits-n4096-p64-point$point.csv"
      taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_ranges" "$bits" 1 16384 8 1 2048 1 "$point" 32768 5 \
        > "$SIXDB_RESULTS/$profile-ranges-k$bits-n16384-p1-point$point.csv"
    done
  done
  for nodes in 1 4096 524288; do
    for mixed in 0 1; do
      for point in 0 1; do
        /usr/bin/time -v taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_cold" "$nodes" "$mixed" 257 "$point" 131072 5 \
          > "$SIXDB_RESULTS/$profile-cold-p$nodes-m$mixed-point$point.csv" \
          2> "$SIXDB_RESULTS/$profile-cold-p$nodes-m$mixed-point$point-memory.txt"
      done
    done
  done
  cp "$dir/compile_commands.json" "$SIXDB_RESULTS/$profile-compile-commands.json"
done
