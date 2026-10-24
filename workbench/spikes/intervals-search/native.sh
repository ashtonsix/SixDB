#!/bin/bash
# Worker runner: build each ISA profile, check answers and guard pages, then
# measure every cell. Usage: worker.py run workbench/spikes/intervals-search/native.sh
set -euo pipefail
case "$(uname -m)" in
 aarch64) profiles=neon; tune=neoverse-v2 ;;
 *) profiles="avx2 avx512"; tune=zen5 ;;
esac
profiles="${SEARCH_PROFILES:-$profiles}"
trials="${SEARCH_TRIALS:-9}"
for profile in $profiles; do
 case "$profile" in
 neon) march=armv8-a+simd ;;
 avx2) march=x86-64-v3 ;;
 avx512) march=znver5 ;;
 esac
 dir="build/intervals-search/$profile"
 out="$SIXDB_RESULTS/$profile"
 mkdir -p "$out"
 cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -gline-tables-only -falign-functions=64 -mllvm -align-all-functions=6 -mllvm -align-all-nofallthru-blocks=6' \
  -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" -DSIXDB_SPIKES=intervals-search > "$out/configure.log"
 cmake --build "$dir" --target intervals_search_bench intervals_search_groups -j "${SIXDB_BUILD_JOBS:-1}" > "$out/build.log" 2>&1
 exe="$dir/workbench/spikes/intervals-search/intervals_search_bench"
 cp "$exe" "$out/"
 llvm-objdump-21 -d --demangle --no-show-raw-insn "$exe" > "$out/bench.s"
 "$exe" check > "$out/check.txt"
 if [ -n "${SEARCH_VALIDATE:-}" ]; then
  cmake -S . -B "$dir" -DSIXDB_SPIKES="intervals-search;intervals;frame-phase-1" > /dev/null
  cmake --build "$dir" --target ikea_intervals_search_check ikea_intervals_sparse_check ikea_intervals_seams_check \
   ikea_intervals_cuts_check ikea_streampack_check ikea_streampack_boundaries_check engine_directory_check \
   frame_checkpoint_point_check \
   -j "${SIXDB_BUILD_JOBS:-1}" >> "$out/build.log" 2>&1
  for t in $(find "$dir" -type f \( -name 'ikea_intervals_*_check' -o -name 'ikea_streampack*_check' -o -name engine_directory_check -o -name frame_checkpoint_point_check \)); do
   echo "== $(basename "$t")" >> "$out/validate.txt"; "$t" >> "$out/validate.txt" 2>&1
  done
 fi
 if [ -z "${SEARCH_SKIP_CELLS:-}" ]; then taskset -c "$SIXDB_CPU" "$exe" run "$trials" > "$out/cells.jsonl"; fi
 taskset -c "$SIXDB_CPU" "$dir/workbench/spikes/intervals-search/intervals_search_groups" "$trials" > "$out/groups.jsonl"
 lscpu > "$out/lscpu.txt" || true
 sha256sum "$exe" > "$out/binary.sha256"
done
