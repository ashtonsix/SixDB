#!/bin/bash
# One captured source, complete operations, native execution; no residence labels.
set -euo pipefail
: "${SIXDB_RESULTS:?}"
: "${SIXDB_CPU:?}"
root=$SIXDB_RESULTS
mkdir -p "$root/bin" "$root/codegen"
progress() {
  python3 -B - "$1" "$root" <<'PY'
import json, os, sys, time
from pathlib import Path
record = dict(phase=sys.argv[1], updated_at=time.time())
Path(sys.argv[2], 'progress.json').write_text(json.dumps(record)+'\n')
if os.environ.get('SIXDB_GROUP_URI'):
    sys.path.insert(0, 'workbench/tools')
    from worker_context import GroupContext
    GroupContext.from_env().progress(sys.argv[1])
PY
}
progress configure
lscpu --json > "$root/lscpu.json"
clang++-21 --version > "$root/compiler.txt"
python3 -B - "$root" "$SIXDB_CPU" <<'PY'
import hashlib, json, os, runpy, sys
from pathlib import Path
root=Path(sys.argv[1])
discover=runpy.run_path('workbench/spikes/memory-characterisation/hardware.py')['discover']
(root/'hardware.json').write_text(json.dumps(discover(int(sys.argv[2])),indent=2)+'\n')
for name,path in [('meminfo-before.txt','/proc/meminfo'),('cgroup-memory-max.txt','/sys/fs/cgroup/memory.max'),
                  ('cgroup-memory-events-before.txt','/sys/fs/cgroup/memory.events')]:
    try: text=Path(path).read_text()
    except OSError as error: text=str(error)+'\n'
    (root/name).write_text(text)
available=int(next(x.split()[1] for x in Path('/proc/meminfo').read_text().splitlines() if x.startswith('MemTotal:')))*1024
limit=Path('/sys/fs/cgroup/memory.max')
if limit.exists() and limit.read_text().strip().isdigit():available=min(available,int(limit.read_text()))
if available < 7*1024**3:raise SystemExit('This surface build needs an 8 GiB class worker; batch TU uses about 3.8 GiB alone.')
files=sorted(p for p in Path('workbench/spikes/hashtable').iterdir() if p.is_file())
(root/'surface-sources.json').write_text(json.dumps({str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in files},indent=2)+'\n')
PY
if [[ $(uname -m) == aarch64 ]]; then
  isa=neon; march=armv8-a+simd; tune=neoverse-v2
else
  isa=${HASHTABLE_ISA:-avx2}; tune=${HASHTABLE_TUNE:-zen5}
  if [[ $isa == avx2 && $tune == zen5 ]]; then march=x86-64-v3
  elif [[ $isa == avx512 && $tune == granite-rapids ]]; then march=graniterapids
  else echo 'This campaign selects Zen5 AVX2, Granite AVX-512, or V2 NEON.' >&2; exit 2
  fi
fi
repeats=${HASHTABLE_REPEATS:-9}
rich_repeats=${HASHTABLE_RICH_REPEATS:-8}
[[ $repeats =~ ^[1-9][0-9]*$ && $rich_repeats =~ ^[1-9][0-9]*$ ]] || exit 2
build="build/hashtable-design-surface-$isa"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_SPIKES=hashtable -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
  > "$root/configure.log" 2>&1
targets=(small small_scan64 small_batch small_publication list_transition rich_composition hash_packet_check links_reservation_check)
# The largest TU uses ~3.8 GiB. Serialize compilation even on a 4-vCPU worker.
for name in "${targets[@]}"; do
  progress "build-$name"
  /usr/bin/time -v -o "$root/build-$name-resources.txt" \
    cmake --build "$build" --target "hashtable_design_$name" -j 1 > "$root/build-$name.log" 2>&1
  exe="$build/workbench/spikes/hashtable/hashtable_design_$name"
  cp "$exe" "$root/bin/$name"
  llvm-objdump-21 -d --demangle --no-show-raw-insn "$exe" | gzip > "$root/codegen/$name.s.gz"
  llvm-nm-21 --print-size --size-sort --demangle "$exe" > "$root/codegen/$name.symbols.txt"
  progress "check-$name"
  args=(check)
  if [[ $name == hash_packet_check || $name == links_reservation_check ]]; then args=(); fi
  /usr/bin/time -v -o "$root/check-$name-resources.txt" \
    taskset -c "$SIXDB_CPU" "$exe" "${args[@]}" > "$root/check-$name.txt" 2>&1
done
cp "$build/compile_commands.json" "$root/compile.json"
python3 -B workbench/spikes/hashtable/surface-report.py "$root" --codegen-only \
  > "$root/codegen-summary.txt"
for name in small small_scan64 small_batch small_publication list_transition; do
  progress "measure-$name"
  /usr/bin/time -v -o "$root/$name-resources.txt" \
    taskset -c "$SIXDB_CPU" "$root/bin/$name" bench "$repeats" > "$root/$name.csv"
done
# Same bounded composition with two explicitly reported footprints. These names
# make no cache claim; the query and correctness state also occupy memory.
for buckets in 1 256; do
  progress "measure-rich-$buckets"
  /usr/bin/time -v -o "$root/rich-$buckets-resources.txt" \
    taskset -c "$SIXDB_CPU" "$root/bin/rich_composition" bench "$buckets" 32 "$rich_repeats" \
    > "$root/rich-$buckets.csv"
done
progress summarize
python3 -B workbench/spikes/hashtable/surface-report.py "$root" > "$root/summary.txt"
if [[ -r /sys/fs/cgroup/memory.events ]]; then
  cat /sys/fs/cgroup/memory.events > "$root/cgroup-memory-events-after.txt"
fi
progress complete
