#!/bin/bash
set -euo pipefail
case "$(uname -m)" in
  aarch64) isa=armv8.2-a+simd; tune=${STREAMPACK_TUNE:-neoverse-v3} ;;
  x86_64) isa=x86-64-v3; tune=${STREAMPACK_TUNE:-zen5} ;;
  *) exit 1 ;;
esac
build="build/clang/streampack-composition-$isa-$tune"; out="$SIXDB_RESULTS/$isa-$tune"
mkdir -p "$out"
python3 workbench/spikes/streampack/references.py > "$out/references.txt"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=streampack \
  -DSIXDB_MARCH="$isa" -DSIXDB_TUNE="$tune" > "$out/configure.log"
targets=(streampack_mutable_population streampack_mutable_inline streampack_mutable_fused
  streampack_lifecycle streampack_lifecycle_prepared streampack_ring streampack_ring_inline)
cmake --build "$build" --target "${targets[@]}" -j "$SIXDB_BUILD_JOBS" > "$out/build.log" 2>&1
bin="$build/workbench/spikes/streampack"
for trial in 0 1; do
  binaries=(streampack_mutable_population streampack_mutable_inline streampack_mutable_fused)
  methods=(direct16 direct32 widths8)
  if [[ $trial == 1 ]]; then binaries=(streampack_mutable_fused streampack_mutable_inline streampack_mutable_population); methods=(widths8 direct32 direct16); fi
  for binary in "${binaries[@]}"; do
    for method in "${methods[@]}"; do
      for sparse in 0 1; do
        key="mutation-$trial-$binary-$method-4194304-$sparse"
        taskset -c "$SIXDB_CPU" "$bin/$binary" "$method" 4194304 256 "$sparse" 2 "$out/$key" mutation > "$out/$key.csv"
      done
    done
  done
  binaries=(streampack_lifecycle streampack_lifecycle_prepared); methods=(direct32 widths8)
  if [[ $trial == 1 ]]; then binaries=(streampack_lifecycle_prepared streampack_lifecycle); methods=(widths8 direct32); fi
  for binary in "${binaries[@]}"; do
    for method in "${methods[@]}"; do
      for publication in direct damage; do
        key="prepared-$trial-$binary-$method-$publication"
        taskset -c "$SIXDB_CPU" "$bin/$binary" "$publication" stable 256 256 48 0 4 256 0 16 "$method" > "$out/$key.csv" 2> "$out/$key.txt"
      done
    done
  done
  binaries=(streampack_ring streampack_ring_inline); methods=(direct16 widths8-native location8 duckdb16)
  variants=(direct manual-hints ring-hints)
  if [[ $trial == 1 ]]; then binaries=(streampack_ring_inline streampack_ring); methods=(duckdb16 location8 widths8-native direct16); variants=(ring-hints manual-hints direct); fi
  for binary in "${binaries[@]}"; do
    for method in "${methods[@]}"; do
      for variant in "${variants[@]}"; do
        key="hint-$trial-$binary-$method-16777216-32-$variant"
        taskset -c "$SIXDB_CPU" "$bin/$binary" "$method" 16777216 256 32 3 "$variant" > "$out/$key.csv"
      done
    done
  done
done
for executable in "${targets[@]}"; do
  cp "$bin/$executable" "$out/$executable"
  sha256sum "$out/$executable" >> "$out/binaries.sha256"
  llvm-objdump-21 --disassemble --demangle "$bin/$executable" > "$out/$executable.asm"
done
