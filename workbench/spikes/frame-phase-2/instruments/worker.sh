#!/bin/bash
set -euo pipefail
study=workbench/spikes/frame-phase-2/instruments
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
  build="build/clang/frame-instruments-$profile"
  out="$SIXDB_RESULTS/$profile"
  mkdir -p "$out/bin"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    '-DSIXDB_SPIKES=frame-phase-1;frame-phase-2;memory-characterisation' \
    -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" > "$out/configure.log" 2>&1
  cmake --build "$build" --target frame_bandwidth_probe frame_bandwidth_check sixdb_pmu_probe memory_characterisation_probe \
    -j "$SIXDB_BUILD_JOBS" > "$out/build.log" 2>&1
  cp "$build/$study/frame_bandwidth_probe" "$out/bin/"
  cp "$build/$study/frame_bandwidth_check" "$out/bin/"
  cp "$build/workbench/spikes/memory-characterisation/memory_characterisation_probe" "$out/bin/"
  cp "$build/compile_commands.json" "$out/"
  cp "$build/CMakeCache.txt" "$out/"
  llvm-objdump-21 -d --demangle "$out/bin/frame_bandwidth_probe" > "$out/linked-assembly.txt"
  python3 "$study/check.py" "$out/bin/frame_bandwidth_probe" --output "$out/checks.json" \
    > "$out/checks.log" 2>&1
  ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 python3 "$study/check.py" \
    "$out/bin/frame_bandwidth_check" --output "$out/sanitize-checks.json" > "$out/sanitize-checks.log" 2>&1
  python3 workbench/tools/pmu/probe.py "$build/workbench/tools/pmu/sixdb_pmu_probe" \
    --cpu "$SIXDB_CPU" --output "$out/pmu" > "$out/pmu.log" 2>&1
  "$build/workbench/spikes/memory-characterisation/memory_characterisation_probe" \
    --suite mlp --quick --cpu "$SIXDB_CPU" --mib 128 --reps 1 --ms 2 \
    --pmu basic --pmu-event l1d-load-misses:3:0x10000 --pmu-output "$out/pmu-memory" \
    > "$out/pmu-memory.csv" 2> "$out/pmu-memory.stderr"
  python3 workbench/spikes/memory-characterisation/pmu_sensitivity.py \
    "$out/pmu-memory" --output "$out/pmu-sensitivity.json"
  if [[ "${CACHE_EVENTS:-0}" == 1 ]]; then
    python3 "$study/cache_events.py" --binary "$out/bin/memory_characterisation_probe" \
      --catalog "$out/pmu/catalog.json" --cpu "$SIXDB_CPU" --output "$out/cache-events"
  fi
  extra=()
  if [[ "${STEADY:-0}" == 1 ]]; then extra+=(--steady); fi
  if [[ -n "${COUNTS:-}" ]]; then extra+=(--counts "$COUNTS"); fi
  if [[ -n "${SIZES:-}" ]]; then extra+=(--sizes "$SIZES"); fi
  if [[ -n "${CHAINS:-}" ]]; then extra+=(--chains "$CHAINS"); fi
  python3 "$study/run.py" --binary "$out/bin/frame_bandwidth_probe" \
    --output "$out/curves" --profile "${PROFILE:-pilot}" --trials "${TRIALS:-4}" \
    --ms "${TARGET_MS:-20}" "${extra[@]}" > "$out/progress.jsonl" 2> "$out/run.stderr"
  python3 "$study/report.py" "$out/curves" > "$out/report.json"
done
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
lscpu -J > "$SIXDB_RESULTS/lscpu.json"
dpkg-query -W libc6 libstdc++6 > "$SIXDB_RESULTS/runtime-packages.txt"
echo complete > "$SIXDB_RESULTS/status.txt"
