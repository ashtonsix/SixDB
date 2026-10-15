#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}"
: "${SIXDB_CPU:?}"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
if [[ $(uname -m) == aarch64 ]]; then
  isa=neon; march=armv8-a+simd; tune=neoverse-v2
else
  isa=${HASHTABLE_ISA:-avx2}; tune=${HASHTABLE_TUNE:?}; march=x86-64-v3
  if [[ $isa == avx512 ]]; then march=graniterapids; fi
fi
build="build/hashtable-design-reduction-paired-$isa"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_SPIKES=hashtable -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
  > "$SIXDB_RESULTS/configure.log" 2>&1
/usr/bin/time -v -o "$SIXDB_RESULTS/build-resources.txt" \
  cmake --build "$build" --target hashtable_design_access hashtable_design_reduction hashtable_design_recipe_view_check -j 1 \
  > "$SIXDB_RESULTS/build.log" 2>&1
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
for contender in borrowed local; do
  target=hashtable_design_access
  if [[ $contender == local ]]; then target=hashtable_design_reduction; fi
  exe="$build/workbench/spikes/hashtable/$target"
  cp "$exe" "$SIXDB_RESULTS/$contender"
  llvm-objdump-21 -d --demangle --no-show-raw-insn "$exe" | gzip > "$SIXDB_RESULTS/$contender-assembly.gz"
  llvm-nm-21 --print-size --size-sort --demangle "$exe" > "$SIXDB_RESULTS/$contender-symbols.txt"
  taskset -c "$SIXDB_CPU" "$exe" check > "$SIXDB_RESULTS/$contender-check.txt"
done
taskset -c "$SIXDB_CPU" "$build/workbench/spikes/hashtable/hashtable_design_recipe_view_check" > "$SIXDB_RESULTS/recipe-check.txt"
/usr/bin/time -v -o "$SIXDB_RESULTS/bench-resources.txt" python3 - "$build/workbench/spikes/hashtable" <<'PY'
import csv, os, random, subprocess, sys
from pathlib import Path
out=Path(os.environ['SIXDB_RESULTS']);bindir=Path(sys.argv[1])
cases=[(k,kind,arm,large) for k in range(8,65,8) for kind in range(3)
       for arm in (0,1,8,9) for large in ((0,) if k==8 else (0,1))]
rng=random.Random(127);rng.shuffle(cases)
with (out/'reduction.csv').open('w') as f, (out/'processes.csv').open('w') as log:
    writer=None;process=0
    log.write('order,pair,within_pair,contender,K,kind,arm,large\n')
    for pair,(k,kind,arm,large) in enumerate(cases):
        contenders=[('borrowed','hashtable_design_access'),('local','hashtable_design_reduction')]
        rng.shuffle(contenders)
        for within,(contender,target) in enumerate(contenders):
            command=['taskset','-c',os.environ['SIXDB_CPU'],str(bindir/target),'run',str(k),str(kind),str(arm),os.environ.get('HASHTABLE_REPEATS','9'),str(large)]
            result=subprocess.run(command,text=True,stdout=subprocess.PIPE,check=True)
            rows=list(csv.DictReader(result.stdout.splitlines()))
            if writer is None:
                writer=csv.DictWriter(f,fieldnames=['contender',*rows[0].keys()]);writer.writeheader()
            for row in rows:writer.writerow({'contender':contender,**row})
            f.flush();log.write(f'{process},{pair},{within},{contender},{k},{kind},{arm},{large}\n');log.flush();process+=1
PY
python3 -B workbench/spikes/hashtable/reduction-report.py "$SIXDB_RESULTS/reduction.csv" \
  --repetitions "${HASHTABLE_REPEATS:-9}" --output "$SIXDB_RESULTS/summary.json" > "$SIXDB_RESULTS/summary.txt"
