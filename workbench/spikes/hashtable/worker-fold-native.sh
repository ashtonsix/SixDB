#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
if [[ $(uname -m) == aarch64 ]]; then march=armv8-a+simd; tune=neoverse-v2
else
  tune=${HASHTABLE_TUNE:?}; march=x86-64-v3
  if [[ ${HASHTABLE_ISA:-avx2} == avx512 ]]; then
    if [[ $tune == zen5 ]]; then march=znver5; else march=graniterapids; fi
  fi
fi
build=build/hashtable-design-worker-fold
targets=(); checks=()
for spec in 40_0_0 40_1_2 64_1_64 40_2_2 64_2_64 8_2_64; do targets+=(hashtable_design_worker_fold_${spec}); done
for spec in 8_0_0 16_1_2 24_2_2 32_0_0 48_1_64 56_2_64; do checks+=(hashtable_design_worker_fold_${spec}); done
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=hashtable \
  -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build "$build" --target "${targets[@]}" "${checks[@]}" -j "${SIXDB_BUILD_JOBS:-1}" > "$SIXDB_RESULTS/build.log" 2>&1
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
mkdir "$SIXDB_RESULTS/bin" "$SIXDB_RESULTS/codegen"
for target in "${checks[@]}" "${targets[@]}"; do
 exe=$build/workbench/spikes/hashtable/$target
 "$exe" check > "$SIXDB_RESULTS/$target-check.csv"
 "$exe" check 1 96 2 > "$SIXDB_RESULTS/$target-small-check.csv"
 if [[ $target != *_fold_8_* ]]; then "$exe" check 1 256 32 > "$SIXDB_RESULTS/$target-crossing-check.csv"; fi
done
for target in "${targets[@]}"; do
 exe=$build/workbench/spikes/hashtable/$target
 cp "$exe" "$SIXDB_RESULTS/bin/$target"
 llvm-objdump-21 -d --demangle --no-show-raw-insn "$exe" | gzip > "$SIXDB_RESULTS/codegen/$target.s.gz"
done
python3 -B - "$SIXDB_RESULTS" "${HASHTABLE_REPEATS:-5}" <<'PY'
import hashlib,json,subprocess,sys,time
from pathlib import Path
root=Path(sys.argv[1]);reps=int(sys.argv[2]);targets=sorted((root/'bin').iterdir())
context={'processes':[],'binary_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in targets}}
for order in range(2):
 for binary in (targets if not order else list(reversed(targets))):
  for n in ([96,128] if '_fold_8_' in binary.name else [96,256,768]):
   for passes in [1,2,32]:
    name=f'{binary.name}-n{n}-p{passes}-order{order}'
    command=['/usr/bin/time','-v','-o',str(root/(name+'-resources.txt')),str(binary),'bench',str(reps),str(n),str(passes)]
    start=time.time()
    with (root/(name+'.csv')).open('w') as out:r=subprocess.run(command,stdout=out,stderr=subprocess.PIPE,text=True)
    context['processes'].append(dict(command=command,start=start,end=time.time(),returncode=r.returncode,stderr=r.stderr))
    (root/'context.json').write_text(json.dumps(context,indent=2)+'\n')
    assert r.returncode==0 and not r.stderr,context['processes'][-1]
PY
python3 -B workbench/spikes/hashtable/worker-fold-report.py "$SIXDB_RESULTS" --output "$SIXDB_RESULTS/qualified" > "$SIXDB_RESULTS/qualified.txt"
