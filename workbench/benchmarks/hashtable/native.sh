#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}" "${SIXDB_CPU:?}"
HASHTABLE_CPU=${HASHTABLE_CPU:-${HASHTABLE_TUNE:?}}
case "$HASHTABLE_CPU" in
 zen5) native=znver5;; granite-rapids) native=graniterapids;; neoverse-v2) native=armv8-a+simd;; *) exit 2;;
esac
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
python3 workbench/benchmarks/hashtable/profiles/generate.py --check
isas=(native);[[ $HASHTABLE_CPU == neoverse-v2 ]] || isas+=(avx2)
common=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_BENCHMARKS=hashtable -DSIXDB_SPIKES= -DSIXDB_TUNE="$HASHTABLE_CPU")
if [[ ${HASHTABLE_COMPARE_CAPTURE:-0} == 1 ]];then
 input=workbench/benchmarks/hashtable/verification/before-source.tar.gz
 [[ $(sha256sum "$input" | cut -d' ' -f1) == f3d85962775d669480fd7fde3eff2bab3bd56252b9d610918fb1cfe31b198b1c ]]
 baseline=build/hashtable-verification-before;mkdir -p "$baseline";tar -xzf "$input" -C "$baseline"
 python3 workbench/benchmarks/hashtable/codegen.py --baseline "$baseline" --output "$SIXDB_RESULTS/codegen/native" --cpu "$HASHTABLE_CPU"
 if [[ $HASHTABLE_CPU != neoverse-v2 ]];then
  python3 workbench/benchmarks/hashtable/codegen.py --baseline "$baseline" --output "$SIXDB_RESULTS/codegen/avx2" --cpu "$HASHTABLE_CPU" --isa avx2
 fi
fi
for isa in "${isas[@]}";do
 march=$native;[[ $isa != avx2 ]] || march=x86-64-v3
 build="build/hashtable-maintained-$isa";out="$SIXDB_RESULTS/$isa";mkdir -p "$out"
 cmake -S . -B "$build" "${common[@]}" -DSIXDB_MARCH="$march" -DCMAKE_CXX_FLAGS=-DNDEBUG > "$out/configure.log" 2>&1
 targets=();for k in 14 30;do for v in 0 8 64;do targets+=("ikea_hashtable_operations_k${k}_b10_v${v}");done;done
 for v in 0 8 64;do targets+=("ikea_hashtable_operations_k30_b18_v${v}");done
 if [[ $isa == native ]];then
  for v in 0 8 64;do targets+=("ikea_hashtable_accumulation_basic_$v");done
  for v in 0 8;do targets+=("ikea_hashtable_accumulation_wide_$v");done
 fi
 cmake --build "$build" --target "${targets[@]}" ikea_hashtable_validate -j "${SIXDB_BUILD_JOBS:-2}" > "$out/build-check.log" 2>&1
 python3 ikea/test/headers.py "$build" --module hashtable > "$out/headers.txt"
 cp "$build/compile_commands.json" "$out/compile.json"
 for target in "${targets[@]}";do
  cp "$build/workbench/benchmarks/hashtable/$target" "$out/$target"
  llvm-objdump-21 -d --demangle "$out/$target" | gzip > "$out/$target.asm.gz"
 done
 printf '%s built\n' "$isa" >> "$SIXDB_RESULTS/progress.txt"
done
# Promotion-only baseline is an external SHA-verified capture, absent from the
# recurring source tree. Its original candidate and controls remain unchanged.
if [[ ${HASHTABLE_COMPARE_CAPTURE:-0} == 1 ]];then
 for panel in basic wide;do
  wide=OFF;[[ $panel != wide ]] || wide=ON
  build="build/hashtable-before-$panel";out="$SIXDB_RESULTS/before-$panel";mkdir -p "$out"
  cmake -S "$baseline" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=hashtable-merge -DSIXDB_BENCHMARKS= \
   -DSIXDB_MARCH="$native" -DSIXDB_TUNE="$HASHTABLE_CPU" -DCMAKE_CXX_FLAGS=-DNDEBUG \
   -DSIXDB_MERGE_ONLY_MODE=18 -DSIXDB_MERGE_WIDE_FAMILIES="$wide" > "$out/configure.log" 2>&1
  cmake --build "$build" --target hashtable_merge_bench_8 -j "${SIXDB_BUILD_JOBS:-2}" > "$out/build.log" 2>&1
  cp "$build/workbench/spikes/hashtable-merge/hashtable_merge_bench_8" "$out/bench"
  cp "$build/compile_commands.json" "$out/compile.json"
 done
fi
# Sanitizers and all fallible construction checks finish before timed processes.
san=build/hashtable-maintained-san
cmake -S . -B "$san" "${common[@]}" -DSIXDB_MARCH="$native" -DCMAKE_CXX_FLAGS_RELEASE='-O1 -g -DNDEBUG' \
 -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fsanitize-address-use-after-return=always -fno-omit-frame-pointer' > "$SIXDB_RESULTS/san-configure.log" 2>&1
checks=(ikea_hashtable_accumulate_check ikea_hashtable_guarded_check ikea_hashtable_modify_check ikea_hashtable_rebuild_storage_check ikea_hashtable_descriptor_check)
cmake --build "$san" --target "${checks[@]}" -j "${SIXDB_BUILD_JOBS:-2}" > "$SIXDB_RESULTS/san-build.log" 2>&1
for target in "${checks[@]}";do
 ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$san/ikea/$target" > "$SIXDB_RESULTS/san-$target.txt"
done
export HASHTABLE_CPU
python3 workbench/benchmarks/hashtable/run-native.py "$SIXDB_RESULTS" "${isas[@]}"
printf '%s maintained complete\n' "$HASHTABLE_CPU" > "$SIXDB_RESULTS/complete.txt"
