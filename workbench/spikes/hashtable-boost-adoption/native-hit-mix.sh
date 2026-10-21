#!/bin/bash
set -euo pipefail
study=workbench/spikes/hashtable-boost-adoption
build="$SIXDB_SOURCE/build/hashtable-hit-mix-native"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
cat /proc/sys/kernel/perf_event_paranoid > "$SIXDB_RESULTS/perf-policy.txt"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0 -DNDEBUG' -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5 -DSIXDB_SPIKES=hashtable-boost-adoption \
  -DMEMORY_CASES='32_11;40_11;40_12' -DMEMORY_WIDTHS='32;40' -DMEMORY_VALUES='0;8' \
  > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build "$build" --target memory_ikea_32_11_0 memory_ikea_32_11_8 \
  memory_ikea_40_12_0 memory_ikea_40_11_8 memory_peers_32_0 memory_peers_32_8 \
  memory_peers_40_0 memory_peers_40_8 sixdb_pmu_probe -j "${SIXDB_BUILD_JOBS:-2}" \
  > "$SIXDB_RESULTS/build.log" 2>&1
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
mkdir -p "$SIXDB_RESULTS/binaries" "$SIXDB_RESULTS/assembly"
for name in memory_ikea_32_11_0 memory_ikea_32_11_8 memory_ikea_40_12_0 memory_ikea_40_11_8 \
            memory_peers_32_0 memory_peers_32_8 memory_peers_40_0 memory_peers_40_8; do
  cp "$build/$study/$name" "$SIXDB_RESULTS/binaries/$name"
  llvm-objdump-21 -Cd --no-show-raw-insn "$build/$study/$name" > "$SIXDB_RESULTS/assembly/$name.asm"
done
python3 workbench/tools/pmu/probe.py "$build/workbench/tools/pmu/sixdb_pmu_probe" \
  --cpu "$SIXDB_CPU" --output "$SIXDB_RESULTS/pmu-probe" \
  --event branches:0:4 --event branch_misses:0:5
python3 "$study/hit-mix-run.py" --binaries "$SIXDB_RESULTS/binaries" \
  --output "$SIXDB_RESULTS/native" --cpu "$SIXDB_CPU"
