#!/bin/bash
set -euo pipefail
reference=workbench/spikes/frame-phase-1/encoding/worker-inputs.json
cp "$reference" "$SIXDB_RESULTS/input-artifact.json"
input_root=$(python3 workbench/tools/artifacts.py input "$reference" --file inputs/cols/OS.u64 --file inputs/cols/UserID.u64 --file inputs/cols/URLHash.u64 --file inputs/cols/EventTime.u64 2> "$SIXDB_RESULTS/input-fetch.txt")
cols="$input_root/inputs/cols"
lscpu -J > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
case "$(uname -m)" in
  x86_64) profiles="avx512 avx2" ;;
  *) profiles="neon" ;;
esac
profiles=${SIXDB_FRAME_PROFILES:-$profiles}
for profile in $profiles; do
  case "$profile" in
    avx512) march=znver5; tune=zen5 ;;
    avx2) march=x86-64-v3; tune=zen5 ;;
    neon) march=armv8-a+simd; tune=neoverse-v2 ;;
    *) printf 'Unknown profile: %s\n' "$profile" >&2; exit 2 ;;
  esac
  build="build/clang/frame-landing-$profile"
  out="$SIXDB_RESULTS/$profile"
  mkdir -p "$out"
  printf '%s configure %s\n' "$(date -u +%FT%TZ)" "$profile" >> "$SIXDB_RESULTS/progress.txt"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    '-DCMAKE_CXX_FLAGS_RELEASE=-O3 -gline-tables-only' \
    -DSIXDB_SPIKES=frame-phase-3 -DSIXDB_TEST_SCOPE=full \
    -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" > "$out/configure.txt" 2>&1
  /usr/bin/time -v cmake --build "$build" --target \
    ikea_frame_description_check ikea_frame_manifest_check ikea_frame_group_image_check \
    ikea_frame_reader_check ikea_frame_stages_check ikea_example_frame_ordinary ikea_example_frame_owner \
    ikea_selection_check ikea_intervals_search_check ikea_intervals_seams_check ikea_intervals_wide_check \
    ikea_seriespack_check ikea_seriespack_descriptor_check ikea_seriespack_admission_check \
    ikea_seriespack_ownership_check ikea_filters_check \
    frame3_native_controls_check frame_landing_measure \
    -j "$SIXDB_BUILD_JOBS" > "$out/build.txt" 2>&1
  sha256sum "$build/workbench/spikes/frame-phase-3/landing/frame_landing_measure" \
    "$build/ikea/libikea_frame.a" "$build/ikea/libikea_seriespack.a" > "$out/binaries.sha256"
  llvm-size-21 "$build/workbench/spikes/frame-phase-3/landing/frame_landing_measure" \
    "$build/ikea/libikea_frame.a" > "$out/text-size.txt"
  printf '%s checks %s\n' "$(date -u +%FT%TZ)" "$profile" >> "$SIXDB_RESULTS/progress.txt"
  ctest --test-dir "$build" -R '^ikea_(frame_.*_check|example_frame_.*|selection_check|intervals_(search|seams|wide)_check|seriespack(_descriptor|_admission|_ownership)?_check|filters_check)$' \
    --output-on-failure --no-tests=error > "$out/checks.txt" 2>&1
  "$build/workbench/spikes/frame-phase-3/controls/frame3_native_controls_check" > "$out/native-controls.txt" 2>&1
  printf '%s measure %s\n' "$(date -u +%FT%TZ)" "$profile" >> "$SIXDB_RESULTS/progress.txt"
  taskset -c "$SIXDB_CPU" "$build/workbench/spikes/frame-phase-3/landing/frame_landing_measure" \
    "$cols" 5 > "$out/measure.jsonl" 2> "$out/measure.err"
  printf '%s done %s\n' "$(date -u +%FT%TZ)" "$profile" >> "$SIXDB_RESULTS/progress.txt"
done
