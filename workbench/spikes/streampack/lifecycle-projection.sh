#!/bin/bash
set -euo pipefail
case "$(uname -m)" in
  aarch64) isa=armv8.2-a+simd; tune=${STREAMPACK_TUNE:-neoverse-v3} ;;
  x86_64) isa=x86-64-v3; tune=${STREAMPACK_TUNE:-zen5} ;;
  *) exit 1 ;;
esac
build="build/clang/streampack-composition-$isa-$tune"; out="$SIXDB_RESULTS/$isa-$tune"
mkdir -p "$out"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=streampack \
  -DSIXDB_MARCH="$isa" -DSIXDB_TUNE="$tune" > "$out/configure.log"
cmake --build "$build" --target streampack_lifecycle streampack_lifecycle_prepared -j "$SIXDB_BUILD_JOBS" > "$out/build.log" 2>&1
bin="$build/workbench/spikes/streampack"
for trial in 0 1; do
  modes=(direct32 widths8 prepared)
  if [[ $trial == 1 ]]; then modes=(prepared widths8 direct32); fi
  for mode in "${modes[@]}"; do
    binary=streampack_lifecycle; recipe=$mode
    if [[ $mode == prepared ]]; then binary=streampack_lifecycle_prepared; recipe=widths8; fi
    key="prepared-$trial-$binary-$recipe-damage-4096"
    taskset -c "$SIXDB_CPU" "$bin/$binary" damage stable 4096 256 48 0 4 1024 0 16 "$recipe" > "$out/$key.csv" 2> "$out/$key.txt"
  done
done
for executable in streampack_lifecycle streampack_lifecycle_prepared; do
  cp "$bin/$executable" "$out/$executable"
  sha256sum "$out/$executable" >> "$out/binaries.sha256"
  llvm-objdump-21 --disassemble --demangle "$bin/$executable" > "$out/$executable.asm"
done
