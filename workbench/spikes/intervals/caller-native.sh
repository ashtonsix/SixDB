#!/bin/bash
set -euo pipefail
# Bounded final-caller audit: actual Frame projection policy, actual Directory
# frozen/maintained reads, current fused StreamPack readers. Warm inputs only.
root="$PWD"
frame_only="${INTERVALS_FRAME_ONLY:-0}"
if [ "$frame_only" = 0 ]; then
python3 workbench/tools/artifacts.py fetch workbench/spikes/intervals/control/pre-graduation-source.json build/intervals-caller-before
before="$root/build/intervals-caller-before"
python3 - "$before" "$SIXDB_RESULTS" <<'PY'
from pathlib import Path
import hashlib,json,sys
old=Path(sys.argv[1]);receipt={}
for folder in ['engine/include','engine/src','orbital/include','orbital/src','loom/include','loom/src']:
 for path in Path(folder).rglob('*'):
  if path.suffix not in {'.h','.cpp'}:continue
  a=(old/path).read_bytes();b=path.read_bytes()
  if str(path)=='engine/src/directory.cpp':
   a=a.replace(b'ikea/boundlist.h',b'ikea/intervals.h').replace(b'ikea::boundlist',b'ikea::intervals').replace(b'Boundlist',b'Intervals').replace(b'as a Intervals',b'as an Intervals')
  assert a==b,'Directory underlying source changed beyond rename: '+str(path)
  receipt[str(path)]=hashlib.sha256(b).hexdigest()
for name in ['engine_reads.cpp','layer.cpp']:
 p=Path('workbench/spikes/segment-directory')/name
 assert (old/p).read_bytes()==p.read_bytes()
Path(sys.argv[2],'directory-source-audit.json').write_text(json.dumps(receipt,indent=2)+'\n')
PY
fi
case "$(uname -m)" in aarch64) profiles=neon; tune=neoverse-v2;; *) profiles="avx2 avx512"; tune=zen5;; esac
for profile in $profiles; do
 case "$profile" in neon) march=armv8-a+simd;; avx2) march=x86-64-v3;; avx512) march=znver5;; esac
 out="$SIXDB_RESULTS/$profile"
 build="$root/build/caller-audit/$profile"
 oldbuild="${before:-}/build/caller-audit/$profile"
 mkdir -p "$out"
 cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -gline-tables-only' \
   -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" '-DSIXDB_SPIKES=frame-phase-1;intervals' -DSIXDB_INTERVALS_FRAME_RUN_AUDIT=ON > "$out/configure.log"
 targets=intervals_frame_runs
 if [ "$frame_only" = 0 ]; then targets="$targets intervals_directory_reads intervals_consumer_seams engine_directory_check"; fi
 cmake --build "$build" --target $targets -j "$SIXDB_BUILD_JOBS" > "$out/build.log" 2>&1
 if [ "$frame_only" = 0 ]; then "$build/engine/engine_directory_check" > "$out/directory-check.log"; fi
 "$build/workbench/spikes/intervals/intervals_frame_runs" --check > "$out/frame-check.jsonl"
 cp "$build/workbench/spikes/intervals/frame-runs/frame-runs-transform.json" "$out/"
 if [ "$frame_only" = 0 ]; then
 cmake -S "$before" -B "$oldbuild" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -gline-tables-only' \
   -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" -DSIXDB_SPIKES=intervals > "$out/before-configure.log"
 cmake --build "$oldbuild" --target intervals_directory_before -j "$SIXDB_BUILD_JOBS" > "$out/before-build.log" 2>&1
 fi
 for exe in $targets; do
  if [ "$exe" = engine_directory_check ]; then continue; fi
  cp "$build/workbench/spikes/intervals/$exe" "$out/$exe"
 done
 if [ "$frame_only" = 0 ]; then cp "$oldbuild/workbench/spikes/intervals/intervals_directory_before" "$out/directory_before"; fi
 cp "$build/compile_commands.json" "$out/compile_commands.json"
 dumps="$targets"; if [ "$frame_only" = 0 ]; then dumps="intervals_frame_runs intervals_directory_reads intervals_consumer_seams directory_before"; fi
 for exe in $dumps; do
  llvm-objdump-21 -d --demangle "$out/$exe" > "$out/$exe.s"
 done
 # Compile outside all timed windows. Separate processes reverse policy order.
 for process in 0 1; do
  taskset -c "$SIXDB_CPU" "$out/intervals_frame_runs" 7 8 "$process" > "$out/frame-p$process.jsonl"
  if [ "$frame_only" = 0 ]; then taskset -c "$SIXDB_CPU" "$out/intervals_consumer_seams" > "$out/seams-p$process.jsonl"; fi
 done
 if [ "$frame_only" = 0 ]; then
 process=0
 for cohort in before after after before; do
  exe="$out/directory_before";if [ "$cohort" = after ];then exe="$out/intervals_directory_reads";fi
  for shape in '4 0 16384' '4 0 1048576' '3 4 1048576'; do
   read -r layer shared count <<< "$shape"
   taskset -c "$SIXDB_CPU" "$exe" "$layer" "$shared" "$count" spread 32768 7 > "$out/directory-p$process-$cohort-l$layer-n$count.csv"
  done
  process=$((process+1))
 done
 fi
 python3 workbench/spikes/intervals/caller-report.py "$out"
 done
