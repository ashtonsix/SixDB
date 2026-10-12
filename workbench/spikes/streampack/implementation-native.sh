#!/bin/bash
set -euo pipefail
phase=${1:-build}
case "$(uname -m)" in
  aarch64) isas=(armv8.2-a+simd); tune=${STREAMPACK_TUNE:-neoverse-v3} ;;
  x86_64) isas=(x86-64-v3 x86-64-v4); tune=${STREAMPACK_TUNE:-zen5} ;;
  *) exit 1 ;;
esac
for isa in "${isas[@]}"; do
  build="build/clang/streampack-implementation-$isa-$tune"; out="$SIXDB_RESULTS/$isa-$tune"
  mkdir -p "$out"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=streampack \
    -DSIXDB_MARCH="$isa" -DSIXDB_TUNE="$tune" > "$out/configure.log"
  cmake --build "$build" --target ikea_streampack_check ikea_streampack_boundaries_check \
    ikea_example_streampack_ordinary ikea_example_streampack_composition streampack_implementation_bench \
    -j "$SIXDB_BUILD_JOBS" > "$out/build.log" 2>&1
  ctest --test-dir "$build" --output-on-failure -R '^ikea_.*streampack' > "$out/checks.txt" 2>&1
  bin="$build/workbench/spikes/streampack/streampack_implementation_bench"
  cp "$bin" "$out/streampack_implementation_bench"
  cp "$build/ikea/ikea_example_streampack_composition" "$out/ikea_example_streampack_composition"
  sha256sum "$bin" "$build/ikea/ikea_example_streampack_composition" > "$out/binaries.sha256"
  llvm-objdump-21 --disassemble --demangle "$bin" > "$out/bench.asm"
  llvm-objdump-21 --disassemble --demangle "$build/ikea/ikea_example_streampack_composition" > "$out/composition.asm"
  cp "$build/compile_commands.json" "$out/compile_commands.json"
  if [[ $phase == bench ]]; then
    for rows in 256 1048576 16777216; do
      for maximum in 255 4095; do
        STREAMPACK_PMU="$out/pmu-$rows-$maximum.jsonl" taskset -c "$SIXDB_CPU" "$bin" "$rows" "$maximum" > "$out/values-$rows-$maximum.csv"
      done
    done
  fi
done
