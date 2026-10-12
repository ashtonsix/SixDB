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
cmake --build "$build" --target streampack_lifecycle streampack_ring streampack_mutable_population \
  streampack_mutable_inline streampack_model_check streampack_geometry streampack_publication_check \
  streampack_widths_publication_check streampack_rewrite_check -j "$SIXDB_BUILD_JOBS" > "$out/build.log" 2>&1
bin="$build/workbench/spikes/streampack"
ctest --test-dir "$build" -R '^streampack_(model|geometry|publication|widths_publication|rewrite)_check$' --output-on-failure > "$out/checks.txt"
# Same fixed population/history/reader-retirement for both recipes. The direct
# publication arm still materializes issued damage; damage consumes afterimages.
for trial in 0 1; do
  recipes=(direct32 widths8); if [[ $trial == 1 ]]; then recipes=(widths8 direct32); fi
  for recipe in "${recipes[@]}"; do
    for objects in 16 4096; do
      for path in direct damage; do
        key="mvcc-$trial-$recipe-$objects-$path"
        taskset -c "$SIXDB_CPU" "$bin/streampack_lifecycle" "$path" stable "$objects" 256 48 0 4 1024 0 16 "$recipe" > "$out/$key.csv" 2> "$out/$key.txt"
      done
    done
    for cap in 128 256 512; do
      key="single-$trial-$recipe-$cap"
      taskset -c "$SIXDB_CPU" "$bin/streampack_lifecycle" damage stable "$((1048576 / cap))" "$cap" 8 0 4 1024 0 1 "$recipe" > "$out/$key.csv" 2> "$out/$key.txt"
    done
  done
done
# Diagnostic clocks are separate from primary comparisons.
for recipe in direct32 widths8; do
  for path in direct damage; do
    key="phases-$recipe-$path"
    STREAMPACK_PHASE_TIMING=1 taskset -c "$SIXDB_CPU" "$bin/streampack_lifecycle" "$path" stable 256 256 48 0 1 128 0 16 "$recipe" > "$out/$key.csv" 2> "$out/$key.txt"
  done
done
# Both visibility policies retain ordinary checks and identical issued effects.
for trial in 0 1; do
  methods=(direct16 direct32 widths8); binaries=(streampack_mutable_population streampack_mutable_inline)
  if [[ $trial == 1 ]]; then methods=(widths8 direct32 direct16); binaries=(streampack_mutable_inline streampack_mutable_population); fi
  for binary in "${binaries[@]}"; do
    for method in "${methods[@]}"; do
      for rows in 16384 4194304; do
        for sparse in 0 1; do
          key="mutation-$trial-$binary-$method-$rows-$sparse"
          taskset -c "$SIXDB_CPU" "$bin/$binary" "$method" "$rows" 256 "$sparse" 2 "$out/$key" mutation > "$out/$key.csv"
        done
      done
    done
  done
done
# Each process owns one representation and uses the identical physical cycle.
# No-SLP applies to every format/route. Configuration is reported separately;
# push/drain and stage transitions are inside the operation timer.
for trial in 0 1; do
  methods=(direct16 widths8-native location8 duckdb16)
  variants=(direct manual manual-hints ring ring-hints)
  if [[ $trial == 1 ]]; then methods=(duckdb16 location8 widths8-native direct16); variants=(ring-hints ring manual-hints manual direct); fi
  for rows in 1024 4194304 16777216; do
    for method in "${methods[@]}"; do
      for chains in 1 8 32; do
        for variant in "${variants[@]}"; do
          key="ring-$trial-$method-$rows-$chains-$variant"
          taskset -c "$SIXDB_CPU" "$bin/streampack_ring" "$method" "$rows" 256 "$chains" 3 "$variant" > "$out/$key.csv"
        done
      done
    done
  done
done
for executable in streampack_lifecycle streampack_ring streampack_mutable_population streampack_mutable_inline; do
  cp "$bin/$executable" "$out/$executable"
  sha256sum "$out/$executable" >> "$out/binaries.sha256"
  llvm-objdump-21 --disassemble --demangle "$bin/$executable" > "$out/$executable.asm"
done
