#!/bin/bash
set -euo pipefail
build=build/clang/streampack-composition-x86-64-v3-granite-rapids
out="$SIXDB_RESULTS/x86-64-v3-granite-rapids"
mkdir -p "$out"
python3 workbench/spikes/streampack/references.py > "$out/references.txt"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=streampack \
  -DSIXDB_MARCH=x86-64-v3 -DSIXDB_TUNE=granite-rapids > "$out/configure.log"
cmake --build "$build" --target streampack_ring_inline -j "$SIXDB_BUILD_JOBS" > "$out/build.log" 2>&1
bin="$build/workbench/spikes/streampack/streampack_ring_inline"
for trial in 0 1; do
  methods=(direct16 widths8-native location8 duckdb16)
  variants=(direct manual manual-hints ring-hints)
  if [[ $trial == 1 ]]; then methods=(duckdb16 location8 widths8-native direct16); variants=(ring-hints manual-hints manual direct); fi
  for method in "${methods[@]}"; do
    for variant in "${variants[@]}"; do
      key="hint-$trial-streampack_ring_inline-$method-67108864-32-$variant"
      taskset -c "$SIXDB_CPU" "$bin" "$method" 67108864 256 32 3 "$variant" > "$out/$key.csv"
    done
  done
done
cp "$bin" "$out/streampack_ring_inline"
sha256sum "$bin" > "$out/binaries.sha256"
llvm-objdump-21 --disassemble --demangle "$bin" > "$out/streampack_ring_inline.asm"
