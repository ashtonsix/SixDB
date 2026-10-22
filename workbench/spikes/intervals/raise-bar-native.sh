#!/bin/bash
set -euo pipefail
case "$(uname -m)" in
 aarch64) profiles=neon; tune=neoverse-v2 ;;
 *) profiles="avx2 avx512"; tune=zen5 ;;
esac
profiles="${INTERVALS_PROFILES:-$profiles}"
python3 workbench/spikes/intervals/control/audit.py
for profile in $profiles; do
 case "$profile" in
 neon) march=armv8-a+simd ;;
 avx2) march=x86-64-v3 ;;
 avx512) march=znver5 ;;
 esac
 dir="build/intervals-raise-bar/$profile"
 out="$SIXDB_RESULTS/$profile"
 mkdir -p "$out"
 cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -gline-tables-only' \
  -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" -DSIXDB_SPIKES=intervals > "$out/configure.log"
 /usr/bin/time -v -o "$out/build-memory.txt" cmake --build "$dir" \
  --target intervals_validate intervals_consumer intervals_latency intervals_consumer_seams intervals_consumer_edits \
  -j "${SIXDB_BUILD_JOBS:-1}" > "$out/build-check.log" 2>&1
 cp "$dir/compile_commands.json" "$out/compile_commands.json"
 for name in intervals_consumer intervals_latency intervals_consumer_seams intervals_consumer_edits; do
  exe="$dir/workbench/spikes/intervals/$name"
  cp "$exe" "$out/$name"
  llvm-objdump-21 -d --demangle "$exe" > "$out/$name.s"
 done
 for process in 0 1; do
  if [ "$process" = 0 ]; then cases="15:256 255:256 4095:256 15:63 255:63 4095:63";
  else cases="4095:63 255:63 15:63 4095:256 255:256 15:256"; fi
  for shape in $cases; do
   maximum="${shape%:*}"; count="${shape#*:}"
   taskset -c "$SIXDB_CPU" "$out/intervals_consumer" --maximum "$maximum" --count "$count" --trials 11 --repeat 64 \
    > "$out/geometry-p$process-m$maximum-n$count.jsonl"
   taskset -c "$SIXDB_CPU" "$out/intervals_latency" "$maximum" "$count" 11 \
    > "$out/chains-p$process-m$maximum-n$count.jsonl"
  done
  taskset -c "$SIXDB_CPU" "$out/intervals_consumer_seams" > "$out/seams-p$process.jsonl"
  taskset -c "$SIXDB_CPU" "$out/intervals_consumer_edits" > "$out/edits-p$process.jsonl"
 done
 python3 - "$out" <<'PY'
import hashlib,json,pathlib,platform,subprocess,sys
p=pathlib.Path(sys.argv[1])
identity={'qualification':'native warm; no inferred residency','platform':platform.platform(),'machine':platform.machine(),
          'compiler':subprocess.check_output(['clang++-21','--version'],text=True),
          'binary_sha256':{f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in p.glob('intervals_*') if f.is_file() and f.suffix!='.s'}}
(p/'identity.json').write_text(json.dumps(identity,indent=2)+'\n')
PY
 python3 workbench/spikes/intervals/raise-bar-report.py "$out"
done
