#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}" "${SIXDB_CPU:?}" "${SIXDB_SOURCE:?}"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
python3 -B - "$SIXDB_CPU" "$SIXDB_RESULTS" <<'PY'
import json,runpy,sys
from pathlib import Path
root=Path(sys.argv[2]);discover=runpy.run_path('workbench/spikes/memory-characterisation/hardware.py')['discover']
(root/'hardware.json').write_text(json.dumps(discover(int(sys.argv[1])),indent=2)+'\n')
for source,name in [('/proc/meminfo','meminfo-before.txt'),('/sys/fs/cgroup/memory.max','cgroup-memory-max.txt')]:
    try:(root/name).write_text(Path(source).read_text())
    except OSError as error:(root/name).write_text(str(error))
PY
if [[ $(uname -m) == aarch64 ]]; then march=armv8-a+simd; tune=neoverse-v2; isa=neon
else march=x86-64-v3; tune=zen5; isa=avx2; fi
build=build/loom-graduation-$isa
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_SPIKES=loom-locality-execution -DSIXDB_LOOM_GRADUATION_ONLY=ON -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
  -DSIXDB_LOOM_CONSUMER_SOURCE_ROOT="$SIXDB_SOURCE" > "$SIXDB_RESULTS/configure.log" 2>&1
targets=(loom_execution_check loom_example_execution loom_graduation_cost_private loom_graduation_cost_product)
for scale in hot large; do for kind in 0 1 2; do
  targets+=(loom_graduation_window_${scale}_${kind})
  for variant in private product; do targets+=(loom_graduation_${scale}_${kind}_${variant}); done
done; done
cmake --build "$build" --target "${targets[@]}" -j "${SIXDB_BUILD_JOBS:-2}" > "$SIXDB_RESULTS/build.log" 2>&1
ctest --test-dir "$build" -R '^loom_(execution_check|example_execution|graduation_(window_(hot|large)_[0-2]|(hot|large)_[0-2]_(private|product))_check)$' \
  --output-on-failure > "$SIXDB_RESULTS/check.txt"
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
bin="$build/workbench/spikes/loom-locality-execution"
for target in "${targets[@]}"; do
  source="$bin/$target"; if [[ $target == loom_execution_check || $target == loom_example_execution ]]; then source="$build/loom/$target"; fi
  cp "$source" "$SIXDB_RESULTS/$target"
  llvm-objdump-21 -d --demangle "$source" | gzip > "$SIXDB_RESULTS/$target.asm.gz"
  if [[ $target == loom_graduation_window_* ]]; then
    kind=$(cut -d_ -f5 <<< "$target"); extra=()
    if [[ $kind != 2 ]]; then extra=(--word-control); fi
    python3 -B workbench/spikes/loom-locality-execution/check_consumer_codegen.py \
      "$SIXDB_RESULTS/$target.asm.gz" "${extra[@]}" > "$SIXDB_RESULTS/$target-codegen.json"
  elif [[ $target == loom_graduation_cost_* ]]; then
    python3 -B workbench/spikes/loom-locality-execution/graduation_codegen.py \
      "$SIXDB_RESULTS/$target.asm.gz" --cost > "$SIXDB_RESULTS/$target-codegen.json"
  elif [[ $target == loom_graduation_* ]]; then
    python3 -B workbench/spikes/loom-locality-execution/graduation_codegen.py \
      "$SIXDB_RESULTS/$target.asm.gz" > "$SIXDB_RESULTS/$target-codegen.json"
  fi
done
for variant in private product; do
  taskset -c "$SIXDB_CPU" "$SIXDB_RESULTS/loom_graduation_cost_$variant" 5 > "$SIXDB_RESULTS/policy-cost-$variant.csv"
done
printf '[]\n' > "$SIXDB_RESULTS/schedule.json"
record_pass() {
  python3 -B - "$SIXDB_RESULTS" "$1" "$2" <<'PY'
import hashlib,json,sys
from pathlib import Path
root=Path(sys.argv[1]);target=sys.argv[2];p=int(sys.argv[3]);name=f'{target}-pass{p}.csv'
path=root/'schedule.json';entries=json.loads(path.read_text())
entries.append(dict(target=target,csv=name,sha256=hashlib.sha256((root/name).read_bytes()).hexdigest(),**{'pass':p}))
path.write_text(json.dumps(entries,indent=2)+'\n')
PY
}
for scale in hot large; do for kind in 0 1 2; do
  if [[ $scale == hot ]]; then n=6144
  elif [[ $kind == 0 ]]; then n=14400000
  else n=3600000; fi
  for pass in 0 1 2 3 4; do
    target=loom_graduation_window_${scale}_${kind}
    concurrency=0; if [[ $scale == large ]]; then concurrency=32; fi
    /usr/bin/time -v -o "$SIXDB_RESULTS/$target-pass$pass-resources.txt" \
      env HT_STAGED_RESIDENCE="$SIXDB_RESULTS/residence-$target-pass$pass" \
      taskset -c "$SIXDB_CPU" "$SIXDB_RESULTS/$target" bench "$n" 1 -1 262144 "$concurrency" \
      > "$SIXDB_RESULTS/$target-pass$pass.csv"
    record_pass "$target" "$pass"
    variants=(private product); if (( pass % 2 )); then variants=(product private); fi
    for variant in "${variants[@]}"; do
      target=loom_graduation_${scale}_${kind}_${variant}
      /usr/bin/time -v -o "$SIXDB_RESULTS/$target-pass$pass-resources.txt" \
        taskset -c "$SIXDB_CPU" "$SIXDB_RESULTS/$target" bench "$n" 1 64 "$pass" \
        > "$SIXDB_RESULTS/$target-pass$pass.csv"
      record_pass "$target" "$pass"
    done
  done
done; done
python3 -B - "$SIXDB_RESULTS" <<'PY'
import gzip,hashlib,json,sys
from pathlib import Path
root=Path(sys.argv[1]);ids={}
for path in root.glob('loom_*'):
    if path.is_file() and not path.suffix:ids[path.name]=hashlib.sha256(path.read_bytes()).hexdigest()
(root/'binaries.json').write_text(json.dumps(ids,indent=2)+'\n')
for path in root.glob('residence-*/*'):
    if path.suffix in ('.smaps','.numa_maps'):
        with gzip.open(str(path)+'.gz','wb') as out:out.write(path.read_bytes())
        path.unlink()
PY
python3 -B workbench/spikes/loom-locality-execution/graduation_report.py "$SIXDB_RESULTS" > "$SIXDB_RESULTS/summary.txt"
