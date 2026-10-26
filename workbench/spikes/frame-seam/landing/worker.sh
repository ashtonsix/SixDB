#!/bin/bash
# Final-source landing qualification, including frozen Frame and leaf controls.
set -euo pipefail
study=workbench/spikes/frame-seam
reference=workbench/spikes/frame-phase-1/encoding/worker-inputs.json
cp "$reference" "$SIXDB_RESULTS/input-artifact.json"
input_root=$(python3 workbench/tools/artifacts.py input "$reference" --file inputs/cols/OS.u64 --file inputs/cols/UserID.u64 --file inputs/cols/URLHash.u64 --file inputs/cols/EventTime.u64 2> "$SIXDB_RESULTS/input-fetch.txt")
cols="$input_root/inputs/cols"
lscpu -J > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
cp "$study/landing/PROVENANCE.json" "$SIXDB_RESULTS/control-source.json"
case "$(uname -m)" in
  x86_64) profiles="avx512 avx2" ;;
  *) profiles="neon" ;;
esac
profiles=${SIXDB_FRAME_PROFILES:-$profiles}
checks=(ikea_frame_description_check ikea_frame_manifest_check ikea_frame_group_image_check
  ikea_frame_reader_check ikea_frame_stages_check ikea_frame_span_check ikea_frame_demand_check
  ikea_example_frame_ordinary ikea_example_frame_owner ikea_selection_check
  ikea_intervals_search_check ikea_intervals_seams_check ikea_intervals_wide_check
  ikea_seriespack_check ikea_seriespack_descriptor_check ikea_seriespack_admission_check
  ikea_seriespack_ownership_check ikea_filters_check)
pattern='^ikea_(frame_.*_check|example_frame_.*|selection_check|intervals_(search|seams|wide)_check|seriespack(_descriptor|_admission|_ownership)?_check|filters_check)$'
# Unchanged backends need the affected Frame surface and direct leaf oracle,
# not another build of the unrelated monolithic width-matrix test.
case "${SIXDB_FRAME_CHECKS:-full}" in
  full) ;;
  core)
    checks=(ikea_frame_description_check ikea_frame_manifest_check ikea_frame_group_image_check
      ikea_frame_reader_check ikea_frame_stages_check ikea_frame_span_check ikea_frame_demand_check
      ikea_example_frame_ordinary ikea_example_frame_owner ikea_selection_check
      ikea_intervals_search_check ikea_intervals_seams_check ikea_intervals_wide_check)
    pattern='^ikea_(frame_.*_check|example_frame_.*|selection_check|intervals_(search|seams|wide)_check)$'
    ;;
  *) exit 2 ;;
esac
printf '%s\n' "${SIXDB_FRAME_CHECKS:-full}" > "$SIXDB_RESULTS/check-scope.txt"
for profile in $profiles; do
  case "$profile" in
    avx512) march=znver5; tune=zen5 ;;
    avx2) march=x86-64-v3; tune=zen5 ;;
    neon) march=armv8-a+simd; tune=neoverse-v2 ;;
    *) exit 2 ;;
  esac
  build="build/clang/frame-seam-landing-$profile"
  out="$SIXDB_RESULTS/$profile"
  mkdir -p "$out"
  printf '%s configure %s\n' "$(date -u +%FT%TZ)" "$profile" >> "$SIXDB_RESULTS/progress.txt"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    '-DCMAKE_CXX_FLAGS=-falign-functions=64 -mllvm -align-all-functions=6 -mllvm -align-all-nofallthru-blocks=6' \
    '-DCMAKE_CXX_FLAGS_RELEASE=-O3 -gline-tables-only' \
    '-DSIXDB_SPIKES=frame-phase-3;frame-seam' -DSIXDB_TEST_SCOPE=full \
    -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" > "$out/configure.txt" 2>&1
  /usr/bin/time -v cmake --build "$build" --target "${checks[@]}" \
    frame3_native_controls_check frame_landing_measure frame_seam_measure frame_seam_span_compare \
    frame_seam_base_span frame_seam_base_measure frame_seam_base_landing \
    frame_seam_qualified_span frame_seam_qualified_measure frame_seam_qualified_landing \
    frame_seam_leaf_old frame_seam_leaf_current frame_seam_mask_kernels -j "$SIXDB_BUILD_JOBS" > "$out/build.txt" 2>&1
  printf '%s checks %s\n' "$(date -u +%FT%TZ)" "$profile" >> "$SIXDB_RESULTS/progress.txt"
  ctest --test-dir "$build" -R "$pattern" --output-on-failure --no-tests=error > "$out/checks.txt" 2>&1
  "$build/workbench/spikes/frame-phase-3/controls/frame3_native_controls_check" > "$out/native-controls.txt" 2>&1
  sha256sum "$build/ikea/libikea_frame.a" "$build/ikea/libikea_seriespack.a" \
    "$build/$study/libframe_seam_base.a" "$build/$study/libframe_seam_qualified.a" \
    "$build/$study/frame_seam_leaf_old" "$build/$study/frame_seam_leaf_current" > "$out/binaries.sha256"
  printf '%s span measure %s\n' "$(date -u +%FT%TZ)" "$profile" >> "$SIXDB_RESULTS/progress.txt"
  python3 "$study/landing/span_pair.py" "$build" "$out/span.jsonl" 5 > "$out/span-progress.txt" 2>&1
  bin="$build/native-pair"
  mkdir -p "$bin/base" "$bin/qualified" "$bin/candidate"
  for arm in base qualified; do
    ln -sf "$(realpath "$build/$study/frame_seam_${arm}_landing")" "$bin/$arm/frame_landing_measure"
    ln -sf "$(realpath "$build/$study/frame_seam_${arm}_measure")" "$bin/$arm/frame_seam_measure"
  done
  ln -sf "$(realpath "$build/workbench/spikes/frame-phase-3/landing/frame_landing_measure")" "$bin/candidate/frame_landing_measure"
  ln -sf "$(realpath "$build/$study/frame_seam_measure")" "$bin/candidate/frame_seam_measure"
  printf '%s regression measure %s\n' "$(date -u +%FT%TZ)" "$profile" >> "$SIXDB_RESULTS/progress.txt"
  SEAM_CPU="$SIXDB_CPU" python3 "$study/compare.py" run "$out/regression.jsonl" 5 "$cols" \
    "base=$bin/base" "qualified=$bin/qualified" "candidate=$bin/candidate" > "$out/regression-progress.txt" 2>&1
  printf '%s leaf measure %s\n' "$(date -u +%FT%TZ)" "$profile" >> "$SIXDB_RESULTS/progress.txt"
  python3 "$study/landing/leaf_pair.py" "$build/$study" "$out/leaf.jsonl" 5 > "$out/leaf-progress.txt" 2>&1
  taskset -c "$SIXDB_CPU" "$build/$study/frame_seam_mask_kernels" "$profile" > "$out/mask-kernels.jsonl"
  printf '%s done %s\n' "$(date -u +%FT%TZ)" "$profile" >> "$SIXDB_RESULTS/progress.txt"
done
