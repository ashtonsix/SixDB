#!/bin/bash
set -euo pipefail
study=workbench/spikes/frame-phase-2/instruments/cells
cp workbench/spikes/frame-phase-1/encoding/worker-inputs.json "$SIXDB_RESULTS/input-artifact.json"
python3 workbench/tools/artifacts.py fetch "$SIXDB_RESULTS/input-artifact.json" "$SIXDB_RESULTS/in" \
  > "$SIXDB_RESULTS/input-fetch.log" 2>&1
cols="$SIXDB_RESULTS/in/inputs/cols"
case "$(uname -m)" in
  x86_64) profiles="${ISAS:-avx2 avx512}" ;;
  *) profiles="${ISAS:-neon}" ;;
esac
for profile in $profiles; do
  case "$profile" in
    avx2) march=x86-64-v3; tune=zen5 ;;
    avx512) march=znver5; tune=zen5 ;;
    neon) march=armv8-a+simd; tune=neoverse-v2 ;;
    *) exit 2 ;;
  esac
  out="$SIXDB_RESULTS/$profile"
  build="build/clang/frame-cells-$profile"
  mkdir -p "$out/bin"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    '-DSIXDB_SPIKES=frame-phase-1;frame-phase-2' -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
    > "$out/configure.log" 2>&1
  cmake --build "$build" --target frame_cells frame_cells_sanitize -j "$SIXDB_BUILD_JOBS" \
    > "$out/build.log" 2>&1
  cp "$build/$study/frame_cells" "$build/$study/frame_cells_sanitize" "$out/bin/"
  cp "$build/compile_commands.json" "$build/CMakeCache.txt" "$out/"
  taskset -c "$SIXDB_CPU" "$out/bin/frame_cells" --check > "$out/check.json"
  ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 taskset -c "$SIXDB_CPU" \
    "$out/bin/frame_cells_sanitize" --check > "$out/sanitize.json"
  llvm-objdump-21 -d --demangle "$out/bin/frame_cells" > "$out/linked-assembly.txt"
  sha256sum "$out/bin/"* > "$out/binaries.sha256"
  python3 "$study/panel.py" --binary "$out/bin/frame_cells" --inputs "$cols" \
    --output "$out/panel" --counts "${COUNTS:-1,8}" --pools "${POOLS:-1,16,512}" \
    --blocks "${BLOCKS:-8}" --ms "${TARGET_MS:-10}" --trials "${TRIALS:-6}" \
    --cases "${CASES:-sum:OS,sum:UserID,pair:UserID:URLHash}" \
    > "$out/progress.jsonl" 2> "$out/panel.stderr"
  sha256sum -c "$out/binaries.sha256" > "$out/binaries-after.txt"
done
lscpu -J > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
dpkg-query -W libc6 libstdc++6 > "$SIXDB_RESULTS/runtime-packages.txt"
echo complete > "$SIXDB_RESULTS/status.txt"
