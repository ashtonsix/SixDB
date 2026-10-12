#!/usr/bin/env bash
set -euo pipefail
out="${SIXDB_RESULTS:-build/allocation-palettes/worker-results}"
build="build/clang/allocation-palettes-performance"
mkdir -p "$out"
case "$(uname -m)" in
  aarch64) march=armv8.2-a+simd; tune=neoverse-v2 ;;
  x86_64) march=znver5; tune=zen5 ;;
  *) exit 2 ;;
esac
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_SPIKES=allocation-palettes -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune"
cmake --build "$build" --target allocation_palettes_timing -j "${SIXDB_BUILD_JOBS:-1}"
{
  uname -a
  getconf PAGESIZE
  clang++-21 --version
  lscpu
} > "$out/host.txt"
python3 -B workbench/spikes/allocation-palettes/performance.py \
  "$build/workbench/spikes/allocation-palettes/allocation_palettes_timing" \
  --output "$out/matrix" --population "${PALETTE_POPULATION:-1024}" \
  --rounds "${PALETTE_ROUNDS:-4}" --repetitions "${PALETTE_REPETITIONS:-3}" \
  --workloads "${PALETTE_WORKLOADS:-mixed,tiny,cluster,log,max}" \
  --cases "${PALETTE_CASES:-}" --alignment "${PALETTE_ALIGNMENT:-16}" \
  --selection "${PALETTE_SELECTION:-unrestricted}" --cpu "${SIXDB_CPU:-0}" "$@"
