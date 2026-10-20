#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}" "${SIXDB_CPU:?}"
spike=workbench/spikes/hashtable-swiss-batch
read -ra variants <<< "${SWISS_BATCH_VARIANTS:-group16 batch64 tail64}"
variant_cmake=$(IFS=\;; echo "${variants[*]}")
arms=$(IFS=,; echo "${variants[*]},boost,abseil")
read -ra cases <<< "${SWISS_BATCH_CASES:-30_10_0 30_10_8 30_10_64 30_20_0 30_20_8}"
case_cmake=$(IFS=\;; echo "${cases[*]}")
case_csv=$(IFS=,; echo "${cases[*]}")
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
for isa in avx512 avx2; do
  out=$SIXDB_RESULTS/$isa
  build=build/hashtable-swiss-batch-$isa
  mkdir -p "$out/codegen" "$out/binaries"
  march=znver5
  [[ $isa == avx512 ]] || march=x86-64-v3
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -g0' \
    -DSIXDB_SPIKES=hashtable-swiss-batch -DSIXDB_BENCHMARKS= -DSWISS_BATCH_VARIANTS="$variant_cmake" -DSWISS_BATCH_CASES="$case_cmake" \
    -DSWISS_BATCH_READ_ONLY="${SWISS_BATCH_READ_ONLY:-ON}" \
    -DSIXDB_MARCH="$march" -DSIXDB_TUNE=zen5 > "$out/configure.log" 2>&1
  targets=(swiss_mask_check)
  for variant in "${variants[@]}"; do targets+=("swiss_check_$variant" "swiss_codegen_$variant" "swiss_trace_$variant"); done
  for case in "${cases[@]}"; do
    for variant in "${variants[@]}" peers; do targets+=("swiss_${variant}_$case"); done
  done
  cmake --build "$build" --target "${targets[@]}" -j "${SIXDB_BUILD_JOBS:-1}" > "$out/build.log" 2>&1
  cp "$build/compile_commands.json" "$out/compile.json"
  timeout 60 "$build/$spike/swiss_mask_check" > "$out/mask-check.txt"
  for variant in "${variants[@]}"; do
    timeout 60 "$build/$spike/swiss_check_$variant" > "$out/check-$variant.txt"
    timeout 60 "$build/$spike/swiss_trace_$variant" > "$out/trace-$variant.txt"
    cp "$build/$spike/$variant/include/delta.json" "$out/delta-$variant.json"
  done
  python3 "$spike/codegen.py" "$build" "$out/codegen"
  for target in "${targets[@]}"; do
    [[ $target != swiss_check_* && $target != swiss_codegen_* && $target != swiss_trace_* && $target != swiss_mask_check ]] || continue
    cp "$build/$spike/$target" "$out/binaries/"
  done
  for variant in "${variants[@]}"; do
    llvm-objdump-21 -d --demangle "$out/binaries/swiss_${variant}_30_10_8" > "$out/$variant-disassembly.txt"
  done
  python3 "$spike/run.py" "$build" "$out/samples" --cpu "$SIXDB_CPU" --arms "$arms" --cases "$case_csv" > "$out/progress.txt" 2>&1
  python3 "$spike/audit.py" "$out"
  python3 "$spike/summarize.py" "$out" --output "$out/summary"
  printf 'complete\n' > "$out/complete.txt"
done
printf 'complete\n' > "$SIXDB_RESULTS/complete.txt"
