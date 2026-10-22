#!/bin/bash
set -euo pipefail
# Actual pre/post ownership move, same worker and reciprocal measurement order.
root="$PWD"
out="$SIXDB_RESULTS"
python3 workbench/tools/artifacts.py fetch workbench/spikes/intervals/control/pre-graduation-source.json build/intervals-pre-graduation
before="$root/build/intervals-pre-graduation"
case "$(uname -m)" in
 aarch64) profiles=neon; tune=neoverse-v2 ;;
 *) profiles="avx2 avx512"; tune=zen5 ;;
esac
for profile in $profiles; do
 case "$profile" in neon) march=armv8-a+simd ;; avx2) march=x86-64-v3 ;; avx512) march=znver5 ;; esac
 for cohort in before after; do
  source="$root"; if [ "$cohort" = before ]; then source="$before"; fi
  build="$source/build/intervals-graduation/$profile"
  result="$out/$profile/$cohort"
  mkdir -p "$result"
  cmake -S "$source" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -gline-tables-only' \
    -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" -DSIXDB_TEST_SCOPE=full -DSIXDB_SPIKES=intervals > "$result/configure.log"
  cmake --build "$build" --target intervals_validate intervals_consumer intervals_latency intervals_consumer_seams intervals_consumer_edits \
    ikea_streampack_encodings_check -j "$SIXDB_BUILD_JOBS" > "$result/build-check.log" 2>&1
  "$build/ikea/ikea_streampack_encodings_check" >> "$result/build-check.log"
  if [ "$cohort" = after ]; then
   cmake --build "$build" --target ikea_intervals_search_check ikea_intervals_sparse_check ikea_intervals_seams_check \
      ikea_example_intervals_ordinary ikea_example_streampack_ordinary ikea_example_streampack_composition \
      -j "$SIXDB_BUILD_JOBS" >> "$result/build-check.log" 2>&1
   ctest --test-dir "$build" --output-on-failure -R '^ikea_(intervals_.*_check|example_intervals_ordinary|example_streampack_(ordinary|composition))$' >> "$result/build-check.log"
  fi
  cp "$build/compile_commands.json" "$result/compile_commands.json"
  for exe in intervals_consumer intervals_latency intervals_consumer_seams intervals_consumer_edits; do
   cp "$build/workbench/spikes/intervals/$exe" "$result/$exe"
   llvm-objdump-21 -d --demangle "$result/$exe" > "$result/$exe.s"
  done
 done
 # Distinct processes in reciprocal before/after/after/before order.
 process=0
 for cohort in before after after before; do
  result="$out/$profile/$cohort"
  if [ "$process" = 0 ] || [ "$process" = 3 ]; then cases="255:256 4095:256 255:63";
  else cases="255:63 4095:256 255:256"; fi
  for shape in $cases; do
   maximum="${shape%:*}"; count="${shape#*:}"
   taskset -c "$SIXDB_CPU" "$result/intervals_consumer" --maximum "$maximum" --count "$count" --trials 11 --repeat 64 > "$result/geometry-p$process-m$maximum-n$count.jsonl"
   taskset -c "$SIXDB_CPU" "$result/intervals_latency" "$maximum" "$count" 11 > "$result/chains-p$process-m$maximum-n$count.jsonl"
  done
  taskset -c "$SIXDB_CPU" "$result/intervals_consumer_seams" > "$result/seams-p$process.jsonl"
  taskset -c "$SIXDB_CPU" "$result/intervals_consumer_edits" > "$result/edits-p$process.jsonl"
  process=$((process+1))
 done
 for cohort in before after; do
  result="$out/$profile/$cohort"
  python3 workbench/spikes/intervals/raise-bar-report.py "$result"
  python3 - "$result" <<'PY'
import hashlib,json,pathlib,subprocess,sys
p=pathlib.Path(sys.argv[1]);(p/'identity.json').write_text(json.dumps({'compiler':subprocess.check_output(['clang++-21','--version'],text=True),'binary_sha256':{f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in p.glob('intervals_*') if f.is_file() and f.suffix!='.s'},'qualification':'same-worker reciprocal order, warm small working set; no inferred cache residence'},indent=2)+'\n')
PY
 done
 done
