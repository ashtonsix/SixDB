#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}"
: "${SIXDB_CPU:?}"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
uname -a > "$SIXDB_RESULTS/kernel.txt"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
cp /proc/meminfo "$SIXDB_RESULTS/meminfo.txt"
if [[ $(uname -m) == aarch64 ]]; then
  tune=neoverse-v2
  variants=(neon)
else
  tune=${HASHTABLE_TUNE:?}
  variants=(avx2 avx512)
fi
for isa in "${variants[@]}"; do
  march=armv8-a+simd
  if [[ $isa == avx2 ]]; then march=x86-64-v3; fi
  if [[ $isa == avx512 ]]; then
    march=$tune
    if [[ $tune == zen5 ]]; then march=znver5; fi
    if [[ $tune == granite-rapids ]]; then march=graniterapids; fi
  fi
  build="build/hashtable-complete-$isa"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSIXDB_SPIKES=hashtable -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
    > "$SIXDB_RESULTS/configure-$isa.log" 2>&1
  cmake --build "$build" --target hashtable_compare hashtable_prior_compare hashtable_width_check ikea_tuplepack_gpr_check -j "${SIXDB_BUILD_JOBS:-1}" \
    > "$SIXDB_RESULTS/build-$isa.log" 2>&1
  exe="$build/workbench/spikes/hashtable/reference/hashtable_compare"
  taskset -c "$SIXDB_CPU" "$exe" check > "$SIXDB_RESULTS/check-$isa.txt"
  taskset -c "$SIXDB_CPU" "$build/workbench/spikes/hashtable/reference/hashtable_width_check" > "$SIXDB_RESULTS/width-check-$isa.txt"
  taskset -c "$SIXDB_CPU" "$build/ikea/ikea_tuplepack_gpr_check" > "$SIXDB_RESULTS/transfer-check-$isa.txt"
  taskset -c "$SIXDB_CPU" "$exe" keys16 7 > "$SIXDB_RESULTS/keys16-$isa.csv"
  llvm-objdump-21 -d --demangle "$exe" > "$SIXDB_RESULTS/assembly-$isa.txt"
  cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile-$isa.json"
  prior="$build/workbench/spikes/hashtable/reference/hashtable_prior_compare"
  taskset -c "$SIXDB_CPU" "$prior" check > "$SIXDB_RESULTS/prior-check-$isa.txt"
  taskset -c "$SIXDB_CPU" "$prior" 4 7 > "$SIXDB_RESULTS/prior-$isa.csv"
  llvm-objdump-21 -d --demangle "$prior" > "$SIXDB_RESULTS/prior-assembly-$isa.txt"
  for n in ${HASHTABLE_SIZES:-128 4096 65536 1048576 4194304}; do
    for slots in ${HASHTABLE_SLOTS:-16}; do
      taskset -c "$SIXDB_CPU" "$exe" "$n" 3 "$slots" \
        > "$SIXDB_RESULTS/$isa-$n-$slots.csv"
    done
  done
done
