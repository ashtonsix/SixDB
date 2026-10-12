#!/bin/bash
set -euo pipefail
case "$(uname -m)" in
  aarch64) isa=armv8.2-a+simd; tune=${STREAMPACK_TUNE:-neoverse-v3} ;;
  x86_64) isa=x86-64-v3; tune=${STREAMPACK_TUNE:-zen5} ;;
  *) exit 1 ;;
esac
build="build/clang/streampack-capacity-$isa-$tune"; out="$SIXDB_RESULTS/$isa-$tune"
mkdir -p "$out"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=streampack \
  -DSIXDB_MARCH="$isa" -DSIXDB_TUNE="$tune" > "$out/configure.log"
cmake --build "$build" --target streampack_lifecycle streampack_sealing -j "$SIXDB_BUILD_JOBS" > "$out/build.log" 2>&1
bin="$build/workbench/spikes/streampack"
# Fixed initial logical population, one edit per publication. Total edits differ
# with Object count, so compare per-edit costs and retain all absolute totals.
# Eight epochs cover four prescribed reader retirement cycles; no hard rewrite
# is expected here. Existing 48-epoch trace owns rank-pressure evidence.
for trial in 0 1 2; do
  capacities=(128 256 512); if [[ $trial == 1 ]]; then capacities=(512 256 128); fi
  for cap in "${capacities[@]}"; do
    taskset -c "$SIXDB_CPU" "$bin/streampack_lifecycle" direct stable "$((1048576 / cap))" "$cap" 8 0 4 1024 0 1 \
      > "$out/single-$trial-$cap.csv" 2> "$out/single-$trial-$cap.txt"
  done
done
# Independent rank/byte limits, exact allocation vs unknown-stream geometric
# growth. Direct32 at the same byte budget isolates endpoint width from sealing.
for cap in 128 256 512; do
  for distribution in 0 1 2; do
    for exact in 0 1; do
      for spec in "2 65535" "4 65535" "4 $((cap * 65535))"; do
        read -r end budget <<< "$spec"
        key="sealing-$end-$cap-$budget-$distribution-$exact"
        taskset -c "$SIXDB_CPU" "$bin/streampack_sealing" "$end" "$cap" "$budget" "$distribution" 8192 "$exact" 3 > "$out/$key.csv"
      done
    done
  done
done
for executable in streampack_lifecycle streampack_sealing; do
  cp "$bin/$executable" "$out/$executable"
  sha256sum "$out/$executable" >> "$out/binaries.sha256"
done
# Granite's reported LLC is 480 MiB. The earlier 16M footprint sweep therefore
# cannot establish beyond-LLC behavior on that host. One candidate per process,
# 64M rows (1.3–2.1 GiB formats), with the competent no-SLP caller for every method.
# Three repetitions; this is one process order, not a paired order-interleaving.
if [[ $tune == granite-rapids ]]; then
  python3 workbench/spikes/streampack/references.py > "$out/references.txt"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=streampack \
    -DSIXDB_MARCH="$isa" -DSIXDB_TUNE="$tune" >> "$out/configure.log"
  cmake --build "$build" --target streampack_footprint_no_slp -j "$SIXDB_BUILD_JOBS" >> "$out/build.log" 2>&1
  for method in direct16 widths8-native location8 duckdb16; do
    key="large-$method-67108864"
    taskset -c "$SIXDB_CPU" "$bin/streampack_footprint_no_slp" "$method" 67108864 256 3 "$out/$key" > "$out/$key.csv"
  done
  cp "$bin/streampack_footprint_no_slp" "$out/streampack_footprint_no_slp"
  sha256sum "$out/streampack_footprint_no_slp" >> "$out/binaries.sha256"
  llvm-objdump-21 --disassemble --demangle "$bin/streampack_footprint_no_slp" > "$out/streampack_footprint_no_slp.asm"
fi
