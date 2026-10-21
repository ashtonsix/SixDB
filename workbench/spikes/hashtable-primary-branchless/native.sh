#!/bin/bash
set -euo pipefail
study=workbench/spikes/hashtable-primary-branchless
harness=workbench/spikes/hashtable-boost-adoption
cohort="${1:-carry}"
case "$cohort" in
 callback) variants='base candidate'; arms=base,candidate,boost,abseil; modes=''; suffix=branchless ;;
 result) variants='base result dummy'; arms=base,result,dummy,boost,abseil; modes='result dummy'; suffix=result ;;
 lean) variants='base select lean'; arms=base,select,lean,boost,abseil; modes=''; suffix=lean ;;
 carry) variants='base carry'; arms=base,carry,boost,abseil; modes=carry; suffix=carry ;;
 *) echo "Unknown cohort: $cohort" >&2; exit 2 ;;
esac
root="$SIXDB_SOURCE/build/hashtable-primary-$suffix-native"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
mkdir -p "$SIXDB_RESULTS/binaries" "$SIXDB_RESULTS/assembly" "$SIXDB_RESULTS/qualification"
bench="$SIXDB_SOURCE/$harness/memory-bench.cpp"
if [[ "$cohort" != callback ]]; then
 bench="$root/result-bench.cpp"
 python3 "$study/bench.py" "$bench"
 cp "$bench" "$root/result-bench.diff" "$root/result-bench.json" "$SIXDB_RESULTS/"
fi
for variant in $variants; do
 build="$root/$variant/build"; overlay=''
 if [[ "$variant" != base ]]; then
  overlay="$root/$variant/include"; mode="$variant"
  if [[ "$variant" == candidate ]]; then mode=callback; fi
  python3 "$study/overlay.py" "$overlay" --records "$SIXDB_RESULTS" --mode "$mode"
 fi
 cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0 -DNDEBUG' -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5 -DSIXDB_SPIKES=hashtable-boost-adoption \
  -DMEMORY_CASES='32_11;40_11;40_12;40_21;40_22;50_21;50_22' \
  -DMEMORY_WIDTHS='32;40;50' -DMEMORY_VALUES='0;8' -DMEMORY_INCLUDE_OVERRIDE="$overlay" \
  -DMEMORY_BENCH_SOURCE="$bench" > "$SIXDB_RESULTS/$variant-configure.log" 2>&1
 targets='memory_ikea_32_11_0 memory_ikea_32_11_8 memory_ikea_40_12_0 memory_ikea_40_11_8'
 if [[ "$cohort" != lean ]]; then targets="$targets memory_ikea_40_22_0 memory_ikea_40_21_8 memory_ikea_50_22_0 memory_ikea_50_21_8"; fi
 if [[ "$variant" == base ]]; then
  targets="$targets memory_peers_32_0 memory_peers_32_8 memory_peers_40_0 memory_peers_40_8 sixdb_pmu_probe"
  if [[ "$cohort" != lean ]]; then targets="$targets memory_peers_50_0 memory_peers_50_8"; fi
 fi
 cmake --build "$build" --target $targets -j "${SIXDB_BUILD_JOBS:-2}" > "$SIXDB_RESULTS/$variant-build.log" 2>&1
 cp "$build/compile_commands.json" "$SIXDB_RESULTS/$variant-compile.json"
 for target in $targets; do
  if [[ "$target" == sixdb_pmu_probe ]]; then continue; fi
  cp "$build/$harness/$target" "$SIXDB_RESULTS/binaries/$variant-$target"
  llvm-objdump-21 -Cd --no-show-raw-insn "$build/$harness/$target" > "$SIXDB_RESULTS/assembly/$variant-$target.asm"
 done
done
python3 workbench/tools/pmu/probe.py "$root/base/build/workbench/tools/pmu/sixdb_pmu_probe" \
 --cpu "$SIXDB_CPU" --output "$SIXDB_RESULTS/pmu-probe" --event branches:0:4 --event branch_misses:0:5
if [[ -n "$modes" ]]; then SIXDB_RESULTS="$SIXDB_RESULTS/qualification" bash "$study/qualify.sh" $modes; fi
extra=(); if [[ "$cohort" == lean ]]; then extra=(--hot-only); fi
python3 "$study/run.py" --binaries "$SIXDB_RESULTS/binaries" --output "$SIXDB_RESULTS/native" \
 --cpu "$SIXDB_CPU" --arms "$arms" "${extra[@]}"
