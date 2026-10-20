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
  cmake --build "$dir" --target boundlist_check boundlist_bench boundlist_packed boundlist_codegen -j "$SIXDB_BUILD_JOBS"
  taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_check" > "$SIXDB_RESULTS/$profile-check.txt"
  exe="$dir/workbench/spikes/boundlist/boundlist_bench"
  case "$tune" in zen5) model=znver5 ;; granite-rapids) model=graniterapids ;; *) model=neoverse-v2 ;; esac
  python3 workbench/spikes/boundlist/audit.py "$dir" "$SIXDB_RESULTS" "$profile" "$model"
  for maximum in 255 4095; do
    for count in 64 256; do
      for nodes in 1 64 4096; do
        for span in 1 257 16384; do
          taskset -c "$SIXDB_CPU" "$exe" "$nodes" "$count" "$maximum" "${BOUNDLIST_OPS:-131072}" "${BOUNDLIST_REPS:-5}" "$span" \
            > "$SIXDB_RESULTS/$profile-m$maximum-n$count-p$nodes-r$span.csv"
        done
        taskset -c "$SIXDB_CPU" "$exe" "$nodes" "$count" "$maximum" "${BOUNDLIST_OPS:-131072}" "${BOUNDLIST_REPS:-5}" 1 1 \
          > "$SIXDB_RESULTS/$profile-m$maximum-n$count-p$nodes-unique.csv"
      done
    done
  done
  for nodes in 1 64 4096; do
    taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_packed" "$nodes" "${BOUNDLIST_OPS:-131072}" "${BOUNDLIST_REPS:-5}" \
      > "$SIXDB_RESULTS/$profile-packed-p$nodes.csv"
  done
  cp "$exe" "$SIXDB_RESULTS/$profile-binary"
  cp "$dir/workbench/spikes/boundlist/boundlist_packed" "$SIXDB_RESULTS/$profile-packed-binary"
  cp "$dir/compile_commands.json" "$SIXDB_RESULTS/$profile-compile-commands.json"
done
