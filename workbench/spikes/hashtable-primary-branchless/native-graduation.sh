#!/bin/bash
set -euo pipefail
study=workbench/spikes/hashtable-primary-branchless
harness=workbench/spikes/hashtable-boost-adoption
grid="${1:-pilot}"
root="$SIXDB_SOURCE/build/hashtable-primary-graduation-native"
mkdir -p "$SIXDB_RESULTS/binaries" "$SIXDB_RESULTS/assembly" "$SIXDB_RESULTS/qualification"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
python3 "$study/hint-bench.py" "$root/hint-bench.cpp"
python3 "$study/hint-bench.py" "$root/original-bench.cpp" --original
cp "$root"/hint-bench.* "$root"/original-bench.* "$SIXDB_RESULTS/"
for variant in original predictable unpredictable adaptive; do
 build="$root/$variant/build";overlay='';bench="$root/hint-bench.cpp";mode=0
 if [[ "$variant" == original ]]; then overlay='';bench="$root/original-bench.cpp";fi
 if [[ "$variant" == unpredictable ]]; then mode=1;fi
 if [[ "$variant" == adaptive ]]; then mode=2;fi
 cases='32_11';widths='32';targets='memory_ikea_32_11_0 memory_ikea_32_11_8'
 if [[ "$grid" == full ]]; then
  cases='32_11;40_11;40_12;40_21;40_22;50_21;50_22';widths='32;40;50'
  targets="$targets memory_ikea_40_12_0 memory_ikea_40_11_8 memory_ikea_40_22_0 memory_ikea_40_21_8 memory_ikea_50_22_0 memory_ikea_50_21_8"
 fi
 if [[ "$variant" == original ]]; then
  targets="$targets memory_peers_32_0 memory_peers_32_8 sixdb_pmu_probe"
  if [[ "$grid" == full ]]; then targets="$targets memory_peers_40_0 memory_peers_40_8 memory_peers_50_0 memory_peers_50_8";fi
 fi
 cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0 -DNDEBUG' -DCMAKE_CXX_FLAGS="-DHINT_MODE=$mode" \
  -DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5 -DSIXDB_SPIKES=hashtable-boost-adoption \
  -DMEMORY_CASES="$cases" -DMEMORY_WIDTHS="$widths" -DMEMORY_VALUES='0;8' \
  -DMEMORY_INCLUDE_OVERRIDE="$overlay" -DMEMORY_BENCH_SOURCE="$bench" \
  > "$SIXDB_RESULTS/$variant-configure.log" 2>&1
 cmake --build "$build" --target $targets -j "${SIXDB_BUILD_JOBS:-2}" > "$SIXDB_RESULTS/$variant-build.log" 2>&1
 cp "$build/compile_commands.json" "$SIXDB_RESULTS/$variant-compile.json"
 for target in $targets;do
  if [[ "$target" == sixdb_pmu_probe ]];then continue;fi
  cp "$build/$harness/$target" "$SIXDB_RESULTS/binaries/$variant-$target"
  llvm-objdump-21 -Cd --no-show-raw-insn "$build/$harness/$target" > "$SIXDB_RESULTS/assembly/$variant-$target.asm"
 done
done
python3 workbench/tools/pmu/probe.py "$root/original/build/workbench/tools/pmu/sixdb_pmu_probe" \
 --cpu "$SIXDB_CPU" --output "$SIXDB_RESULTS/pmu-probe" --event branches:0:4 --event branch_misses:0:5
cmake --build "$root/adaptive/build" --target ikea_hashtable_validate -j "${SIXDB_BUILD_JOBS:-2}" > "$SIXDB_RESULTS/qualification/release.txt" 2>&1
SIXDB_RESULTS="$SIXDB_RESULTS/qualification" bash "$study/graduation-qualify.sh"
python3 "$study/hint-run.py" --binaries "$SIXDB_RESULTS/binaries" --output "$SIXDB_RESULTS/native" --cpu "$SIXDB_CPU" --grid "$grid"
