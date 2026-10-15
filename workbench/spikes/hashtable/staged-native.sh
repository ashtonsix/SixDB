#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}" "${SIXDB_CPU:?}"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
python3 -B - "$SIXDB_CPU" "$SIXDB_RESULTS" <<'PY'
import json,runpy,sys
from pathlib import Path
root=Path(sys.argv[2]);discover=runpy.run_path('workbench/spikes/memory-characterisation/hardware.py')['discover']
(root/'hardware.json').write_text(json.dumps(discover(int(sys.argv[1])),indent=2)+'\n')
for source,name in [('/proc/meminfo','meminfo-before.txt'),('/sys/fs/cgroup/memory.max','cgroup-memory-max.txt')]:
    try:(root/name).write_text(Path(source).read_text())
    except OSError as e:(root/name).write_text(str(e))
PY
if [[ $(uname -m) == aarch64 ]]; then
  march=armv8-a+simd; tune=neoverse-v2; isa=neon
else
  tune=${HASHTABLE_TUNE:?}; isa=${HASHTABLE_ISA:-avx2}; march=x86-64-v3
  if [[ $isa == avx512 ]]; then
    if [[ $tune == zen5 ]]; then march=znver5; else march=graniterapids; fi
  fi
fi
build=build/hashtable-design-staged-$isa
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=hashtable \
  -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" > "$SIXDB_RESULTS/configure.log" 2>&1
scales=(${HT_STAGED_SCALES:-hot large})
kinds=(${HT_STAGED_KINDS:-0 1 2})
for kind in "${kinds[@]}"; do [[ $kind == 0 || $kind == 1 || $kind == 2 ]]; done
targets=()
for scale in "${scales[@]}"; do for kind in "${kinds[@]}"; do targets+=(hashtable_design_staged_${scale}_${kind}); done; done
for kind in 0 1 2; do targets+=(hashtable_design_staged_forced_$kind); done
for key in 8 16 24 32 40 48 56 64; do targets+=(hashtable_design_staged_width_$key); done
cmake --build "$build" --target "${targets[@]}" -j "${SIXDB_BUILD_JOBS:-1}" > "$SIXDB_RESULTS/build.log" 2>&1
ctest --test-dir "$build" -R '^hashtable_design_staged_(forced_[012]|width_[0-9]+)_check$' \
  --output-on-failure > "$SIXDB_RESULTS/check.txt"
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
# Build/check/disassemble all contenders before any timing.
for scale in "${scales[@]}"; do for kind in "${kinds[@]}"; do
  name=hashtable_design_staged_${scale}_${kind}
  cp "$build/workbench/spikes/hashtable/$name" "$SIXDB_RESULTS/$name"
  taskset -c "$SIXDB_CPU" "$SIXDB_RESULTS/$name" check > "$SIXDB_RESULTS/$name-check.csv"
  llvm-objdump-21 -d --demangle "$SIXDB_RESULTS/$name" | gzip > "$SIXDB_RESULTS/$name.asm.gz"
  extra=(); if [[ $kind != 2 ]]; then extra=(--word-control); fi
  python3 -B workbench/spikes/hashtable/check_staged_codegen.py "$SIXDB_RESULTS/$name.asm.gz" "${extra[@]}" \
    > "$SIXDB_RESULTS/$name-codegen.json"
done; done
for scale in "${scales[@]}"; do for kind in "${kinds[@]}"; do
  name=hashtable_design_staged_${scale}_${kind}
  if [[ $scale == hot ]]; then n=6144; elif [[ $scale == large ]]; then
    if [[ $kind == 0 ]]; then n=14400000; else n=3600000; fi
  elif [[ $kind == 0 ]]; then n=115200000; else n=57600000; fi
  /usr/bin/time -v -o "$SIXDB_RESULTS/$name-resources.txt" env HT_STAGED_RESIDENCE="$SIXDB_RESULTS/residence" \
    taskset -c "$SIXDB_CPU" "$SIXDB_RESULTS/$name" bench "$n" "${HASHTABLE_REPEATS:-3}" \
    > "$SIXDB_RESULTS/$name.csv"
done; done
python3 -B - "$SIXDB_RESULTS" <<'PY'
import gzip,hashlib,json,sys
from pathlib import Path
root=Path(sys.argv[1]);ids={}
for path in root.glob('hashtable_design_staged_*'):
    if path.is_file() and not path.suffix and not path.name.endswith('resources'):ids[path.name]=hashlib.sha256(path.read_bytes()).hexdigest()
(root/'binaries.json').write_text(json.dumps(ids,indent=2)+'\n')
for path in (root/'residence').glob('*'):
    if path.suffix in ('.smaps','.numa_maps'):
        with gzip.open(str(path)+'.gz','wb') as out:out.write(path.read_bytes())
        path.unlink()
PY
python3 -B workbench/spikes/hashtable/staged-report.py "$SIXDB_RESULTS" > "$SIXDB_RESULTS/summary.txt"
