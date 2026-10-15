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
build=build/hashtable-design-consolidation
targets=()
specs=(8_0_0 40_1_2 64_1_64 8_2_64 40_2_2 64_2_64)
if [[ ${HASHTABLE_CONSOLIDATION_PANEL:-core} == patches ]]; then
  specs=(8_2_64 8_2_64_patch 40_2_2 40_2_2_patch 64_2_64 64_2_64_patch)
elif [[ ${HASHTABLE_CONSOLIDATION_PANEL:-core} == encoded ]]; then
  specs=(8_0_0 40_0_0 40_1_2 64_1_64 8_2_64_patch 40_2_2_patch 64_2_64_patch)
fi
for spec in "${specs[@]}"; do
  targets+=(hashtable_design_consolidation_${spec})
done
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
python3 -B - "$SIXDB_RESULTS" "${HASHTABLE_REPEATS:-5}" "${HASHTABLE_HISTORIES:-16}" "$SIXDB_CPU" <<'PY'
import csv,hashlib,json,subprocess,sys,time
from pathlib import Path
root=Path(sys.argv[1]);reps=int(sys.argv[2]);histories=int(sys.argv[3]);cpu=sys.argv[4]
targets=sorted((root/'bin').iterdir())
context={'processes':[],'binary_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in targets}}
for order in range(2):
    for binary in (targets if not order else list(reversed(targets))):
        output=binary.name+f'-order{order}.csv'
        resources=root/(binary.name+f'-order{order}-resources.txt')
        command=['/usr/bin/time','-v','-o',str(resources),'taskset','-c',cpu,str(binary),'bench',str(reps),str(histories)]
        start=time.time()
        with (root/output).open('w') as out:
            result=subprocess.run(command,stdout=out,stderr=subprocess.PIPE,text=True)
        context['processes'].append(dict(command=command,start=start,end=time.time(),returncode=result.returncode,stderr=result.stderr))
        (root/'context.json').write_text(json.dumps(context,indent=2)+'\n')
        assert result.returncode==0 and not result.stderr,context['processes'][-1]
        rows=list(csv.DictReader((root/output).open()));assert rows
        assert len(rows)==(1 if rows[0]['K']=='8' else 2)*(3 if rows[0]['kind']=='2' else 1)*2*2*reps*6*2
        assert all(int(r['histories'])==histories and float(r['merge_ns'])>0 for r in rows)
print(f'{len(context["processes"])} complete paired processes; every timed history checks complete bytes')
PY
