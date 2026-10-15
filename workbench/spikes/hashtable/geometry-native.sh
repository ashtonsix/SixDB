#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}"
: "${SIXDB_CPU:?}"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
if [[ $(uname -m) == aarch64 ]]; then
  isa=neon; march=armv8-a+simd; tune=neoverse-v2
else
  isa=${HASHTABLE_ISA:-avx2}; tune=${HASHTABLE_TUNE:?}; march=x86-64-v3
  if [[ $isa == avx512 ]]; then
    if [[ $tune == zen5 ]]; then march=znver5; else march=graniterapids; fi
  fi
fi
build="build/hashtable-design-geometry-$isa"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_SPIKES=hashtable -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
  > "$SIXDB_RESULTS/configure.log" 2>&1
targets=(hashtable_design_recipe_view_check)
for key in 16 40 64; do
  for width in 0 2 8 16; do
    targets+=(hashtable_design_geometry_${key}_${width})
  done
done
/usr/bin/time -v -o "$SIXDB_RESULTS/build-resources.txt" \
  cmake --build "$build" --target "${targets[@]}" -j 1 > "$SIXDB_RESULTS/build.log" 2>&1
ctest --test-dir "$build" -R '^hashtable_design_recipe_view_check$' \
  --output-on-failure > "$SIXDB_RESULTS/check.txt"
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
for key in 16 40 64; do
  for width in 0 2 8 16; do
    exe="$build/workbench/spikes/hashtable/hashtable_design_geometry_${key}_${width}"
    cp "$exe" "$SIXDB_RESULTS/geometry-$key-$width"
    llvm-objdump-21 -d --demangle --no-show-raw-insn "$exe" | gzip > "$SIXDB_RESULTS/assembly-$key-$width.gz"
    llvm-nm-21 --print-size --size-sort --demangle "$exe" > "$SIXDB_RESULTS/symbols-$key-$width.txt"
  done
done
/usr/bin/time -v -o "$SIXDB_RESULTS/bench-resources.txt" \
  python3 -B workbench/spikes/hashtable/geometry-run.py "$SIXDB_RESULTS" "$SIXDB_RESULTS" \
    --cpu "$SIXDB_CPU" --keys 16 40 64 --passes 2 --reps "${HASHTABLE_REPEATS:-5}"
python3 -B workbench/spikes/hashtable/geometry-report.py "$SIXDB_RESULTS" \
  --output "$SIXDB_RESULTS/summary.json" --verify-binaries > "$SIXDB_RESULTS/summary.txt"
