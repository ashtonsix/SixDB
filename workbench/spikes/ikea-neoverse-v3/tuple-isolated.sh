#!/bin/bash
set -euo pipefail
out="$SIXDB_RESULTS"
progress() {
  python3 - "$1" <<'PY'
import sys
sys.path.insert(0, 'workbench/tools')
from worker_context import GroupContext
GroupContext.from_env().progress(sys.argv[1])
PY
}
archive="$PWD/build/ikea-neoverse-v3/original-source.tar.gz"
mkdir -p "$(dirname "$archive")"
python3 - "$archive" <<'PY'
import hashlib,json,subprocess,sys
from pathlib import Path
ref=json.loads(Path('workbench/spikes/ikea-neoverse-v3/original-source.json').read_text())
p=Path(sys.argv[1])
if not p.exists() or hashlib.sha256(p.read_bytes()).hexdigest()!=ref['sha256']:
    subprocess.run(['aws','s3','cp',ref['uri'],str(p),'--only-show-errors'],check=True)
assert hashlib.sha256(p.read_bytes()).hexdigest()==ref['sha256']
PY
for profile in v2-neon v3-neon; do
  progress "building-$profile"
  tune=neoverse-v3; march=armv8.2-a+simd
  if [[ "$profile" == v2-neon ]]; then tune=neoverse-v2; fi
  for variant in stock promoted; do
    build="build/ikea-neoverse-v3/final-$profile-$variant"
    result="$out/$profile/$variant"
    mkdir -p "$result"
    original=""; if [[ "$variant" == stock ]]; then original="$archive"; fi
    cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DSIXDB_SPIKES=ikea-neoverse-v3 '-DSIXDB_BENCHMARKS=bec256;tuplepack' \
      -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" -DV3_SPIKE_TUNE="$tune" \
      -DV3_BEC_BASELINE_ARCHIVE="$original" > "$result/configure.log" 2>&1
    cmake --build "$build" --target ikea_tuplepack_bench ikea_tuplepack_word_bench \
      ikea_tuplepack_gpr_check ikea_tuplepack_packets_check ikea_tuplepack_wire_check \
      ikea_tuplepack_operations_check ikea_tuplepack_execution_check \
      -j "$SIXDB_BUILD_JOBS" > "$result/build.log" 2>&1
    for check in ikea_tuplepack_gpr_check ikea_tuplepack_packets_check ikea_tuplepack_wire_check ikea_tuplepack_operations_check ikea_tuplepack_execution_check; do
      taskset -c "$SIXDB_CPU" "$build/ikea/$check" >> "$result/checks.txt" 2>&1
    done
    python3 ikea/test/headers.py "$build" --module tuplepack >> "$result/checks.txt" 2>&1
    for binary in ikea_tuplepack_bench ikea_tuplepack_word_bench; do
      cp "$build/workbench/benchmarks/tuplepack/$binary" "$result/"
    done
    llvm-objdump-21 -d --demangle "$build/ikea/libikea_tuplepack.a" > "$result/tuple-kernels.asm"
    cp "$build/compile_commands.json" "$result/"
  done
done
for trial in 1 2; do
  progress "measuring-trial-$trial"
  profiles='v2-neon v3-neon'; variants='stock promoted'
  if (( trial % 2 == 0 )); then profiles='v3-neon v2-neon'; variants='promoted stock'; fi
  for profile in $profiles; do
    for variant in $variants; do
      result="$out/$profile/$variant"
      for suite in tuple words; do
        args=()
        if [[ "$suite" == tuple ]]; then binary=ikea_tuplepack_bench; fi
        if [[ "$suite" == words ]]; then
          binary=ikea_tuplepack_word_bench
          args+=('--benchmark_filter=^word/(1|2|4)/unit(4|16|64)/stride(4|16|64)/map(0|2)/random/all/(word_hash|update)/(gpr|gpr_native|simd|simd_word)$')
        fi
        TUPLEPACK_WORD_BROAD=1 taskset -c "$SIXDB_CPU" "$result/$binary" "${args[@]}" \
          --benchmark_min_time=0.03s --benchmark_repetitions=5 \
          --benchmark_enable_random_interleaving=false --benchmark_out_format=json \
          --benchmark_out="$result/$suite-$trial.json" > "$result/$suite-$trial.log" 2>&1
      done
    done
  done
done
progress sanitizers
for tune in neoverse-v2 neoverse-v3; do
  build="build/ikea-neoverse-v3/final-$tune-asan"
  result="$out/sanitizers/$tune"
  mkdir -p "$result"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DSIXDB_MARCH=armv8.2-a+simd -DSIXDB_TUNE="$tune" \
    '-DCMAKE_CXX_FLAGS=-O1 -fsanitize=address,undefined -fno-omit-frame-pointer' > "$result/configure.log" 2>&1
  cmake --build "$build" --target \
    ikea_tuplepack_gpr_check ikea_tuplepack_packets_check ikea_tuplepack_wire_check \
    ikea_tuplepack_operations_check ikea_tuplepack_execution_check \
    -j "$SIXDB_BUILD_JOBS" > "$result/build.log" 2>&1
  for check in tuplepack_gpr tuplepack_packets tuplepack_wire tuplepack_operations tuplepack_execution; do
    "$build/ikea/ikea_${check}_check" >> "$result/checks.txt" 2>&1
  done
done
progress complete
