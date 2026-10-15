#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}"
: "${SIXDB_CPU:?}"
compress_residence() {
  python3 -B - "$SIXDB_RESULTS/residence" <<'PY'
import gzip
from pathlib import Path
import sys
for path in Path(sys.argv[1]).glob('*'):
    if path.suffix in ('.smaps', '.numa_maps'):
        with gzip.open(str(path) + '.gz', 'wb') as target:
            target.write(path.read_bytes())
        path.unlink()
PY
}
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
python3 -B - "$SIXDB_RESULTS" "$SIXDB_CPU" <<'PY'
import json
from pathlib import Path
import runpy
import sys

root, cpu = Path(sys.argv[1]), int(sys.argv[2])
discover = runpy.run_path('workbench/spikes/memory-characterisation/hardware.py')['discover']
(root / 'hardware.json').write_text(json.dumps(discover(cpu), indent=2) + '\n')
for source, name in [('/proc/meminfo', 'meminfo-before.txt'),
                     ('/sys/fs/cgroup/memory.max', 'cgroup-memory-max.txt'),
                     ('/sys/fs/cgroup/memory.events', 'cgroup-memory-events-before.txt')]:
    try:
        content = Path(source).read_text()
    except OSError as error:
        content = str(error) + '\n'
    (root / name).write_text(content)
PY
# Reuse the existing kernel-access probe. It tests whether the guest permits
# counting, not whether this workload was served by DRAM. No policy changes.
clang++-21 -std=c++23 -O2 -fno-fast-math -ffp-contract=off \
  workbench/benchmarks/orbital/pmu_probe.cpp workbench/benchmarks/orbital/pmu.cpp \
  -o "$SIXDB_RESULTS/orbital_pmu_probe"
python3 -B workbench/benchmarks/orbital/pmu_probe.py "$SIXDB_RESULTS/orbital_pmu_probe" \
  --cpu "$SIXDB_CPU" --output "$SIXDB_RESULTS/pmu" \
  --event l1d-load-misses:3:0x10000 --event llc-load-misses:3:0x10002 \
  > "$SIXDB_RESULTS/pmu-probe.txt"
if [[ $(uname -m) == aarch64 ]]; then
  isa=neon; march=armv8-a+simd; tune=neoverse-v2
else
  isa=${HASHTABLE_ISA:-avx2}; tune=${HASHTABLE_TUNE:?}; march=x86-64-v3
  if [[ $isa == avx512 ]]; then
    if [[ $tune == zen5 ]]; then march=znver5; else march=graniterapids; fi
  fi
fi
build="build/hashtable-design-$isa"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_SPIKES=hashtable -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
  > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build "$build" --target hashtable_design_ring -j "${SIXDB_BUILD_JOBS:-2}" \
  > "$SIXDB_RESULTS/build.log" 2>&1
exe="$build/workbench/spikes/hashtable/hashtable_design_ring"
taskset -c "$SIXDB_CPU" "$exe" check > "$SIXDB_RESULTS/check.csv"
python3 workbench/spikes/hashtable/ring-report.py "$SIXDB_RESULTS/check.csv" --mode check \
  > "$SIXDB_RESULTS/check.txt"
taskset -c "$SIXDB_CPU" "$exe" generator-check > "$SIXDB_RESULTS/generator-check.txt"
env HASHTABLE_RING_RESIDENCE="$SIXDB_RESULTS/residence" \
  taskset -c "$SIXDB_CPU" "$exe" dram-check > "$SIXDB_RESULTS/dram-check.csv"
compress_residence
python3 workbench/spikes/hashtable/ring-report.py "$SIXDB_RESULTS/dram-check.csv" --mode dram-check \
  > "$SIXDB_RESULTS/dram-check.txt"
cp "$exe" "$SIXDB_RESULTS/hashtable_design_ring"
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
llvm-objdump-21 -d --demangle "$exe" | gzip > "$SIXDB_RESULTS/assembly.gz"
python3 workbench/spikes/hashtable/check_ring_codegen.py "$SIXDB_RESULTS/assembly.gz" \
  > "$SIXDB_RESULTS/codegen.json"
for mode in ${HASHTABLE_RING_MODES:-hot llc large}; do
  /usr/bin/time -v -o "$SIXDB_RESULTS/$mode-resources.txt" \
    env HASHTABLE_RING_RESIDENCE="$SIXDB_RESULTS/residence" \
    taskset -c "$SIXDB_CPU" "$exe" "$mode" "${HASHTABLE_REPEATS:-5}" > "$SIXDB_RESULTS/$mode.csv"
  compress_residence
  python3 workbench/spikes/hashtable/ring-report.py "$SIXDB_RESULTS/$mode.csv" --mode "$mode" \
    --output "$SIXDB_RESULTS/$mode-summary.json" > "$SIXDB_RESULTS/$mode-summary.txt"
done
if [[ -r /sys/fs/cgroup/memory.events ]]; then
  cat /sys/fs/cgroup/memory.events > "$SIXDB_RESULTS/cgroup-memory-events-after.txt"
fi
