#!/bin/bash
set -euo pipefail
progress() {
  PYTHONPATH=workbench/tools python3 -c 'from worker_context import GroupContext; import sys; GroupContext.from_env().progress(sys.argv[1],case=sys.argv[2])' "$1" "$2"
}
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
  progress building "$profile"
  cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0' \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" -DSIXDB_SPIKES=boundlist
  cmake --build "$dir" --target ikea_boundlist_check ikea_streampack_check ikea_streampack_boundaries_check \
    ikea_example_boundlist_ordinary ikea_example_boundlist_composition boundlist_material boundlist_reads \
    boundlist_promotion boundlist_mutation boundlist_cold -j "$SIXDB_BUILD_JOBS"
  ctest --test-dir "$dir" --output-on-failure -R '^ikea_(boundlist|streampack|example_boundlist)' > "$SIXDB_RESULTS/$profile-check.txt"
  taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_material" > "$SIXDB_RESULTS/$profile-material-check.txt"
  for target in reads promotion mutation cold; do
    exe="$dir/workbench/spikes/boundlist/boundlist_$target"
    python3 workbench/spikes/boundlist/qualification-audit.py "$exe" "$SIXDB_RESULTS/$profile-$target-assembly.txt"
    cp "$exe" "$SIXDB_RESULTS/$profile-$target-binary"
  done
  progress measuring "$profile-search"
  for nodes in 1 64 4096; do
    for maximum in 255 4095; do
      for point in 0 1; do
        taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_promotion" "$nodes" "$maximum" "$point" 262144 5 \
          > "$SIXDB_RESULTS/$profile-promotion-p$nodes-w$maximum-point$point.csv"
      done
    done
    taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_mutation" "$((nodes<8?8:nodes))" 32768 5 \
      > "$SIXDB_RESULTS/$profile-mutation-p$nodes.csv"
  done
  progress measuring "$profile-readers"
  for nodes in 1 64; do
    taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_reads" "$nodes" 262144 5 \
      > "$SIXDB_RESULTS/$profile-reads-p$nodes.csv"
  done
  BOUNDLIST_READ_LARGE_ONLY=1 taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_reads" 4096 1048576 5 \
    > "$SIXDB_RESULTS/$profile-reads-p4096.csv"
  progress measuring "$profile-cold"
  for nodes in 1 4096 524288; do
    if [ "$nodes" = 524288 ]; then ops=2097152; else ops=131072; fi
    for mixed in 0 1; do
      for point in 0 1; do
        /usr/bin/time -v taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_cold" "$nodes" "$mixed" 257 "$point" "$ops" 3 \
          > "$SIXDB_RESULTS/$profile-cold-p$nodes-m$mixed-point$point.csv" \
          2> "$SIXDB_RESULTS/$profile-cold-p$nodes-m$mixed-point$point-memory.txt"
      done
    done
  done
  cp "$dir/compile_commands.json" "$SIXDB_RESULTS/$profile-compile-commands.json"
  progress qualified "$profile"
done
