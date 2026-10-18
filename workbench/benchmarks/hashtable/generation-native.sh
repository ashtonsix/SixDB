#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}" "${SIXDB_CPU:?}" "${GENERATION_CPU:?}"
case "$GENERATION_CPU" in zen4) march=znver4;; zen5) march=znver5;; *) exit 2;; esac
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
# Deliberately transfer the existing Zen5 recipe: this tests generation transfer,
# without selecting winners from a fresh encoding search on the older CPU.
for isa in avx512 avx2-gfni avx2;do
 flags=-DNDEBUG;isa_march=$march
 if [[ $isa != avx512 ]];then isa_march=x86-64-v3;fi
 if [[ $isa == avx2-gfni ]];then flags+=' -mgfni';fi
 for k in 14 30;do
  build="build/hashtable-generation-$GENERATION_CPU-$isa-k$k"
  out="$SIXDB_RESULTS/$isa/k$k";mkdir -p "$out"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES= -DSIXDB_BENCHMARKS=hashtable \
   -DSIXDB_MARCH="$isa_march" -DSIXDB_TUNE="$GENERATION_CPU" -DCMAKE_CXX_FLAGS="$flags" \
   -DIKEA_HASHTABLE_W="$((k+10))" -DIKEA_HASHTABLE_B=10 > "$out/configure.log" 2>&1
  cmake --build "$build" --target ikea_hashtable_reads -j "${SIXDB_BUILD_JOBS:-2}" > "$out/build.log" 2>&1
  exe="$build/workbench/benchmarks/hashtable/ikea_hashtable_reads"
  cp "$build/compile_commands.json" "$out/compile.json"
  llvm-objdump-21 -d --demangle "$exe" | gzip > "$out/reads.asm.gz"
  taskset -c "$SIXDB_CPU" "$exe" --operation check --cpu zen5 --isa "$isa" > "$out/check.csv"
  for seed in 713 3191 76543;do for miss in 0 50 100;do
   taskset -c "$SIXDB_CPU" "$exe" --operation bench --cpu zen5 --isa "$isa" --seed "$seed" \
    --repetitions 5 --requests 1048576 --miss-percent "$miss" --conditioning-bytes 0 > "$out/read-$seed-$miss.csv"
  done;done
 done
 printf '%s completed\n' "$isa" >> "$SIXDB_RESULTS/progress.txt"
done
printf '%s transferred-Zen5-recipe complete\n' "$GENERATION_CPU" > "$SIXDB_RESULTS/complete.txt"
