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
if [[ $(uname -m) == aarch64 ]]; then march=armv8-a+simd; tune=neoverse-v2
else
  tune=${HASHTABLE_TUNE:?}; march=x86-64-v3
  if [[ ${HASHTABLE_ISA:-avx2} == avx512 ]]; then
    if [[ $tune == zen5 ]]; then march=znver5; else march=graniterapids; fi
  fi
fi
build=build/hashtable-design-read-windows
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=hashtable \
  -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" > "$SIXDB_RESULTS/configure.log" 2>&1
cases=(${HT_WINDOWS_CASES:-40_0_0 40_2_1 64_64_1 40_2_2 64_64_2})
levels=(${HT_WINDOWS_LEVELS:-10 18});extras=(${HT_WINDOWS_WIDE_LEVELS:-});targets=();populations=();checks=()
for level in "${levels[@]}"; do for case in "${cases[@]}"; do
  for slots in 14 28; do for mode in 0 1 2; do
    targets+=(hashtable_design_staged_windows_a${level}_${case}_${slots}_${mode});populations+=($((6<<level)))
  done; done
done; done
for level in "${extras[@]}"; do for slots in 14 28; do for mode in 0 1 2; do
  targets+=(hashtable_design_staged_windows_a${level}_64_64_1_${slots}_${mode});populations+=($((6<<level)))
done; done; done
for key in 8 16 24 32 40 48 56 64; do checks+=(hashtable_design_staged_windows_width_$key); done
checks+=(hashtable_design_staged_windows_publication)
cmake --build "$build" --target "${targets[@]}" "${checks[@]}" -j "${SIXDB_BUILD_JOBS:-1}" > "$SIXDB_RESULTS/build.log" 2>&1
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
ctest --test-dir "$build" -R '^hashtable_design_staged_windows_(width_[0-9]+_check|publication)$' \
  --output-on-failure > "$SIXDB_RESULTS/boundaries.txt"
for target in "${targets[@]}"; do
  cp "$build/workbench/spikes/hashtable/$target" "$SIXDB_RESULTS/$target"
  taskset -c "$SIXDB_CPU" "$SIXDB_RESULTS/$target" check > "$SIXDB_RESULTS/$target-check.csv"
  llvm-objdump-21 -d --demangle "$SIXDB_RESULTS/$target" | gzip > "$SIXDB_RESULTS/$target.asm.gz"
  python3 -B workbench/spikes/hashtable/check_staged_codegen.py "$SIXDB_RESULTS/$target.asm.gz" \
    --report-ready-frame > "$SIXDB_RESULTS/$target-codegen.json"
done
# Paired policies have the same live owner cohort; each binary rotates methods
# and contenders. Rotate mode order by family without concurrent measurements.
for ((group=0;group<${#targets[@]};group+=3)); do
  order=(0 1 2);if ((group%6)); then order=(2 1 0);fi
  for offset in "${order[@]}"; do
    index=$((group+offset));target=${targets[index]};n=${populations[index]}
    /usr/bin/time -v -o "$SIXDB_RESULTS/$target-resources.txt" env HT_STAGED_RESIDENCE="$SIXDB_RESULTS/residence-$target" \
      taskset -c "$SIXDB_CPU" "$SIXDB_RESULTS/$target" bench "$n" "${HASHTABLE_REPEATS:-3}" \
      > "$SIXDB_RESULTS/$target.csv"
  done
done
python3 -B workbench/spikes/hashtable/staged-report.py "$SIXDB_RESULTS" > "$SIXDB_RESULTS/summary.txt"
python3 -B workbench/spikes/hashtable/read-windows-report.py "$SIXDB_RESULTS" > "$SIXDB_RESULTS/windows.txt"
python3 -B - "$SIXDB_RESULTS" <<'PY'
import gzip,hashlib,json,sys
from pathlib import Path
root=Path(sys.argv[1]);ids={}
for path in root.glob('hashtable_design_staged_windows_*'):
    if path.is_file() and not path.suffix:ids[path.name]=hashlib.sha256(path.read_bytes()).hexdigest()
(root/'binaries.json').write_text(json.dumps(ids,indent=2)+'\n')
for path in root.glob('residence-*/*'):
    if path.suffix in ('.smaps','.numa_maps'):
        with gzip.open(str(path)+'.gz','wb') as out:out.write(path.read_bytes())
        path.unlink()
PY
