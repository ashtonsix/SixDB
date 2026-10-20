#!/bin/bash
set -euo pipefail
case "$(uname -m)" in
  aarch64) profiles=neon; tune=neoverse-v2 ;;
  *) profiles="avx2 avx512"; tune="${BOUNDLIST_TUNE:-zen5}" ;;
esac
progress() {
  PYTHONPATH=workbench/tools python3 -c 'from worker_context import GroupContext; import sys; GroupContext.from_env().progress(sys.argv[1],case=sys.argv[2])' "$1" "$2"
}
for profile in $profiles; do
  case "$profile" in
    neon) march=armv8-a+simd ;;
    avx2) march=x86-64-v3 ;;
    avx512) if [ "$tune" = zen5 ]; then march=znver5; else march=graniterapids; fi ;;
  esac
  dir="build/boundlist-ownership/$profile"
  progress building "$profile"
  cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0' \
    -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" -DSIXDB_SPIKES=boundlist -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
  cmake --build "$dir" --target ikea_boundlist_check ikea_streampack_check ikea_streampack_boundaries_check \
    ikea_example_streampack_composition boundlist_material boundlist_reads boundlist_mutation boundlist_packing \
    boundlist_u_before_reads boundlist_u_before_mutation boundlist_u_before_packing boundlist_encoding \
    boundlist_ownership_packets boundlist_u_before_packets -j "$SIXDB_BUILD_JOBS"
  ctest --test-dir "$dir" --output-on-failure -R '^ikea_(boundlist|streampack|example_streampack_composition)' > "$SIXDB_RESULTS/$profile-check.txt"
  taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_material" > "$SIXDB_RESULTS/$profile-material.txt"
  cp "$dir/compile_commands.json" "$SIXDB_RESULTS/$profile-compile-commands.json"
  for consumer in reads mutation packing encoding ownership_packets; do
    exe="$dir/workbench/spikes/boundlist/boundlist_$consumer"
    llvm-objdump-21 --no-show-raw-insn -Cd "$exe" > "$SIXDB_RESULTS/$profile-$consumer-assembly.txt"
    cp "$exe" "$SIXDB_RESULTS/$profile-$consumer-binary"
  done
  progress measuring "$profile-ownership"
  for rep in 0 1 2 3 4; do
    if (( rep % 2 )); then order="after before"; else order="before after"; fi
    for variant in $order; do
      stem=boundlist
      if [ "$variant" = before ]; then stem=boundlist_u_before; fi
      for nodes in 1 64 4096; do
        if [ "$nodes" = 1 ]; then
          taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/${stem}_reads" "$nodes" 131072 1 > "$SIXDB_RESULTS/$profile-$variant-reads-p$nodes-r$rep.csv"
        else
          BOUNDLIST_READ_LARGE_ONLY=1 taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/${stem}_reads" "$nodes" 131072 1 > "$SIXDB_RESULTS/$profile-$variant-reads-p$nodes-r$rep.csv"
        fi
        if [ "$variant" = before ]; then packet_exe=boundlist_u_before_packets; else packet_exe=boundlist_ownership_packets; fi
        taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/$packet_exe" "$nodes" 65536 1 > "$SIXDB_RESULTS/$profile-$variant-packets-p$nodes-r$rep.csv"
      done
      taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/${stem}_mutation" 64 16384 1 > "$SIXDB_RESULTS/$profile-$variant-mutation-r$rep.csv"
      taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/${stem}_packing" 32 1 > "$SIXDB_RESULTS/$profile-$variant-packing-r$rep.csv"
    done
  done
  if [ "${BOUNDLIST_OWNERSHIP_ONLY:-0}" = 1 ]; then
    progress qualified "$profile-ownership"
    continue
  fi
  progress measuring "$profile-encoding"
  for count in 1 15 16 17 31 32 64 128 256; do
    for order in 0 1; do
      taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_encoding" "$count" 1 131072 3 "$order" > "$SIXDB_RESULTS/$profile-encoding-n$count-p1-o$order.csv"
    done
  done
  for nodes in 64 4096 1048576; do
    for order in 0 1; do
      if [ "$nodes" = 1048576 ]; then ops=2097152; else ops=131072; fi
      /usr/bin/time -v taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/boundlist/boundlist_encoding" 256 "$nodes" "$ops" 3 "$order" > "$SIXDB_RESULTS/$profile-encoding-n256-p$nodes-o$order.csv" 2> "$SIXDB_RESULTS/$profile-encoding-n256-p$nodes-o$order-memory.txt"
    done
  done
  progress qualified "$profile"
done
