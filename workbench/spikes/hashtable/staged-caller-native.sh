#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}" "${SIXDB_CPU:?}"
# Matched caller-input prefetch, not another table format. Measure C32 mixed
# graphs where the external successor load exists, retaining all methods.
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
python3 -B - "$SIXDB_CPU" "$SIXDB_RESULTS" <<'PY'
import json,runpy,sys
from pathlib import Path
discover=runpy.run_path('workbench/spikes/memory-characterisation/hardware.py')['discover']
(Path(sys.argv[2])/'hardware.json').write_text(json.dumps(discover(int(sys.argv[1])),indent=2)+'\n')
PY
if [[ $(uname -m) == aarch64 ]]; then march=armv8-a+simd; tune=neoverse-v2; isa=neon
else
  tune=${HASHTABLE_TUNE:?}; isa=${HASHTABLE_ISA:-avx2}; march=x86-64-v3
  if [[ $isa == avx512 ]]; then
    if [[ $tune == zen5 ]]; then march=znver5; else march=graniterapids; fi
  fi
fi
build=build/hashtable-design-staged-$isa
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=hashtable \
  -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" > "$SIXDB_RESULTS/configure.log" 2>&1
targets=()
for kind in 0 1 2; do for suffix in '' _graph; do targets+=(hashtable_design_staged_large_${kind}${suffix}); done; done
cmake --build "$build" --target "${targets[@]}" -j 1 > "$SIXDB_RESULTS/build.log" 2>&1
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
for kind in 0 1 2; do for suffix in '' _graph; do
  name=hashtable_design_staged_large_${kind}${suffix}
  cp "$build/workbench/spikes/hashtable/$name" "$SIXDB_RESULTS/$name"
  taskset -c "$SIXDB_CPU" "$SIXDB_RESULTS/$name" check > "$SIXDB_RESULTS/$name-check.csv"
  llvm-objdump-21 -d --demangle "$SIXDB_RESULTS/$name" | gzip > "$SIXDB_RESULTS/$name.asm.gz"
  extra=(); if [[ $kind != 2 ]]; then extra=(--word-control); fi
  python3 -B workbench/spikes/hashtable/check_staged_codegen.py "$SIXDB_RESULTS/$name.asm.gz" "${extra[@]}" \
    > "$SIXDB_RESULTS/$name-codegen.json"
done; done
for kind in 0 1 2; do
  n=3600000; if [[ $kind == 0 ]]; then n=14400000; fi
  # Alternate which representation runs first; every binary also rotates its
  # owner/method order. No mutation runs concurrently with these timings.
  suffixes=('' _graph); if [[ $kind == 1 ]]; then suffixes=(_graph ''); fi
  for suffix in "${suffixes[@]}"; do
    name=hashtable_design_staged_large_${kind}${suffix}
    /usr/bin/time -v -o "$SIXDB_RESULTS/$name-resources.txt" env HT_STAGED_RESIDENCE="$SIXDB_RESULTS/residence-$name" \
      taskset -c "$SIXDB_CPU" "$SIXDB_RESULTS/$name" bench "$n" "${HASHTABLE_REPEATS:-3}" 1 262144 32 \
      > "$SIXDB_RESULTS/$name.csv"
  done
done
python3 -B workbench/spikes/hashtable/staged-report.py "$SIXDB_RESULTS" --graphs 1 --concurrencies 32 > "$SIXDB_RESULTS/summary.txt"
python3 -B - "$SIXDB_RESULTS" <<'PY'
import gzip,hashlib,json,sys
from pathlib import Path
root=Path(sys.argv[1]);ids={}
for path in root.glob('hashtable_design_staged_*'):
    if path.is_file() and not path.suffix:ids[path.name]=hashlib.sha256(path.read_bytes()).hexdigest()
(root/'binaries.json').write_text(json.dumps(ids,indent=2)+'\n')
for path in root.glob('residence-*/*'):
    if path.suffix in ('.smaps','.numa_maps'):
        with gzip.open(str(path)+'.gz','wb') as out:out.write(path.read_bytes())
        path.unlink()
PY
