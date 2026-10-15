#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}" "${SIXDB_CPU:?}"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
if [[ $(uname -m) == aarch64 ]]; then march=armv8-a+simd; tune=neoverse-v2
else
  tune=${HASHTABLE_TUNE:?}; march=x86-64-v3
  if [[ ${HASHTABLE_ISA:-avx2} == avx512 ]]; then
    if [[ $tune == zen5 ]]; then march=znver5; else march=graniterapids; fi
  fi
fi
build=build/hashtable-design-prepared-list
targets=()
for key in 8 16 24 32 40 48 56 64; do targets+=(hashtable_design_prepared_list_$key); done
targets+=(hashtable_design_prepared_list_40_checked)
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=hashtable \
  -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build "$build" --target "${targets[@]}" -j "${SIXDB_BUILD_JOBS:-1}" > "$SIXDB_RESULTS/build.log" 2>&1
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
mkdir "$SIXDB_RESULTS/bin" "$SIXDB_RESULTS/codegen"
for target in "${targets[@]}"; do
  exe=$build/workbench/spikes/hashtable/$target
  cp "$exe" "$SIXDB_RESULTS/bin/$target"
  taskset -c "$SIXDB_CPU" "$exe" check > "$SIXDB_RESULTS/$target-check.txt"
  llvm-objdump-21 -d --demangle --no-show-raw-insn "$exe" | gzip > "$SIXDB_RESULTS/codegen/$target.s.gz"
done
python3 -B - "$SIXDB_RESULTS" "$SIXDB_CPU" "${HASHTABLE_REPEATS:-7}" "${HASHTABLE_HISTORIES:-64}" <<'PY'
import csv,hashlib,json,subprocess,sys,time
from pathlib import Path
root=Path(sys.argv[1]);cpu=sys.argv[2];reps=int(sys.argv[3]);histories=int(sys.argv[4])
assert reps>0 and histories>0
targets=sorted((root/'bin').iterdir())
context={'processes':[],'binary_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in targets}}
for order in range(2):
    for binary in (targets if not order else list(reversed(targets))):
        name=binary.name+f'-order{order}.csv'
        command=['taskset','-c',cpu,str(binary),'bench',str(reps),str(histories)]
        start=time.time()
        with (root/name).open('w') as out:
            result=subprocess.run(command,stdout=out,stderr=subprocess.PIPE,text=True)
        context['processes'].append(dict(command=command,start=start,end=time.time(),returncode=result.returncode,stderr=result.stderr))
        (root/'context.json').write_text(json.dumps(context,indent=2)+'\n')
        assert result.returncode==0 and not result.stderr,context['processes'][-1]
        rows=list(csv.DictReader((root/name).open()))
        expected={(str(kind),str(w),arm,str(n),str(grain),str(rep))
                  for kind,w in [(0,0),(1,2),(1,64),(2,2),(2,64)]
                  for arm in ['synchronous','prepared','abseil','abseil_exact']
                  for n in [16,128,240] for grain in [8,64,256] for rep in range(reps)}
        seen=set()
        for row in rows:
            k=tuple(row[f] for f in ['kind','W','arm','keys','grain','rep'])
            assert k not in seen;seen.add(k)
            assert int(row['histories'])==histories and float(row['lifetime_ns'])>0
            if row['arm']=='synchronous':assert int(row['coexist_logical'])==-1
            if row['arm'].startswith('abseil'):assert int(row['after_backing'])==-1
            if row['arm']=='prepared':assert int(row['coexist_logical'])>int(row['after_logical'])
        assert seen==expected,(name,seen^expected)
print(f'{len(context["processes"])} complete processes; complete lifetime and storage cells verified')
PY
