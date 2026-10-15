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
build=build/hashtable-design-worker-epochs
targets=()
checks=()
case ${HASHTABLE_EPOCH_PANEL:-original} in
  original)
    for key in 8 16 24 32 40 48 56 64; do
      for spec in 0_0 1_2 1_64 2_2 2_64; do targets+=(hashtable_design_worker_epochs_${key}_${spec}); done
    done;;
  policy)
    # Paired attribution, with all kinds and domain/value extremes. This is
    # a targeted panel, not a substitute for the all-domain original campaign.
    for spec in 8_0_0 40_1_2 64_1_64 8_2_64 40_2_2 64_2_64; do
      targets+=(hashtable_design_worker_epochs_${spec})
      for policy in 1 2 3 4; do
        if [[ $spec != *_2_* && ( $policy == 2 || $policy == 3 ) ]]; then continue; fi
        targets+=(hashtable_design_worker_epochs_${spec}_policy${policy})
      done
    done;;
  partial)
    for spec in 40_0_0 40_1_2 64_1_64 8_2_64 40_2_2 64_2_64; do
      for policy in 4 5; do targets+=(hashtable_design_worker_epochs_${spec}_policy${policy}); done
    done;;
  packet)
    for spec in 40_0_0 40_1_2 64_1_64 8_2_64 40_2_2 64_2_64; do
      for suffix in '' _packet1 _packet2; do targets+=(hashtable_design_worker_epochs_${spec}_policy5${suffix}); done
    done
    for spec in 8_1 8_64 16_2 24_3 32_7 40_16 48_8 56_32 64_1 64_64; do
      checks+=(hashtable_design_combine_${spec} hashtable_design_combine_${spec}_scalar_packet)
    done;;
  growth|growth-edge)
    for spec in 40_0_0 40_1_2 64_1_64 8_2_64 40_2_2 64_2_64; do
      for suffix in '' _growth1 _growth2; do targets+=(hashtable_design_worker_epochs_${spec}_policy5_packet2${suffix}); done
    done
    for spec in 8_0_0 16_1_2 24_2_2 32_0_0 48_1_64 56_2_64; do
      checks+=(hashtable_design_worker_epochs_${spec}_policy5_packet2_growth2)
    done;;
  format)
    for spec in 40_0_0 40_1_2 64_1_64 8_2_64 40_2_2 64_2_64; do
      for suffix in '' _slots14 _slots28; do targets+=(hashtable_design_worker_epochs_${spec}_policy5_packet2_growth2${suffix}); done
    done
    for spec in 8_0_0 16_1_2 24_2_2 32_0_0 48_1_64 56_2_64; do
      checks+=(hashtable_design_worker_epochs_${spec}_policy5_packet2_growth2_slots14)
    done;;
  *) echo 'Unknown HASHTABLE_EPOCH_PANEL'  >&2; exit 2;;
esac
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=hashtable \
  -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build "$build" --target "${targets[@]}" "${checks[@]}" -j "${SIXDB_BUILD_JOBS:-1}" > "$SIXDB_RESULTS/build.log" 2>&1
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
mkdir "$SIXDB_RESULTS/bin" "$SIXDB_RESULTS/codegen"
for target in "${checks[@]}"; do
  "$build/workbench/spikes/hashtable/$target" check > "$SIXDB_RESULTS/$target-check.txt"
done
for target in "${targets[@]}"; do
  exe=$build/workbench/spikes/hashtable/$target
  cp "$exe" "$SIXDB_RESULTS/bin/$target"
  "$exe" check > "$SIXDB_RESULTS/$target-check.csv"
  if [[ ${HASHTABLE_EPOCH_PANEL:-original} == partial || ${HASHTABLE_EPOCH_PANEL:-original} == packet || ${HASHTABLE_EPOCH_PANEL:-original} == growth || ${HASHTABLE_EPOCH_PANEL:-original} == growth-edge || ${HASHTABLE_EPOCH_PANEL:-original} == format ]]; then
    "$exe" check 1 256 > "$SIXDB_RESULTS/$target-check-small.csv"
  fi
  llvm-objdump-21 -d --demangle --no-show-raw-insn "$exe" | gzip > "$SIXDB_RESULTS/codegen/$target.s.gz"
done
# Each executable pins two owner threads and a distinct coordinator CPU itself.
# Pinning the process to SIXDB_CPU would remove the required worker topology.
python3 -B - "$SIXDB_RESULTS" "${HASHTABLE_REPEATS:-5}" <<'PY'
import csv,hashlib,json,os,subprocess,sys,time
from pathlib import Path
root=Path(sys.argv[1]);reps=int(sys.argv[2]);assert reps>0
targets=sorted((root/'bin').iterdir())
context={'processes':[],'binary_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in targets}}
for order in range(2):
    for binary in (targets if not order else list(reversed(targets))):
      panel=os.getenv('HASHTABLE_EPOCH_PANEL')
      populations=([256] if '_epochs_8_' in binary.name else [512,640,1024]) if panel=='growth' else (([256] if '_epochs_8_' in binary.name else [256,1024]) if panel=='packet' else (([240,256] if '_epochs_8_' in binary.name else [240,256,257,1024]) if panel=='partial' else [None]))
      if panel=='growth-edge':
        with (root/(binary.name+'-check.csv')).open() as f:
          geometry=next(csv.DictReader(f))
        populations=[256 if geometry['K']=='8' else int(geometry['first_capacity'])*90//100]
        assert 256 <= populations[0] <= 1024
      if panel=='format': populations=[256] if '_epochs_8_' in binary.name else [512,768,1024]
      for n in populations:
       for passes in ([8,128] if panel in ['packet','growth','growth-edge','format'] else [8]):
        output=binary.name+(f'-n{n}' if n else '')+(f'-p{passes}' if panel in ['packet','growth','growth-edge','format'] else '')+f'-order{order}.csv'
        resources=root/(output.removesuffix('.csv')+'-resources.txt')
        command=['/usr/bin/time','-v','-o',str(resources),str(binary),'bench',str(reps)]+([str(n),str(passes)] if panel in ['packet','growth','growth-edge','format'] else ([str(n)] if n else []))
        start=time.time()
        with (root/output).open('w') as out:
            result=subprocess.run(command,stdout=out,stderr=subprocess.PIPE,text=True)
        context['processes'].append(dict(command=command,start=start,end=time.time(),returncode=result.returncode,stderr=result.stderr))
        (root/'context.json').write_text(json.dumps(context,indent=2)+'\n')
        assert result.returncode==0 and not result.stderr,context['processes'][-1]
print(f'{len(context["processes"])} complete paired processes; qualifying operation/storage cells')
PY
python3 -B workbench/spikes/hashtable/worker-epochs-report.py "$SIXDB_RESULTS" \
  --output "$SIXDB_RESULTS/qualified" > "$SIXDB_RESULTS/qualified.txt"

if [[ ${HASHTABLE_EPOCH_PANEL:-original} == packet ]]; then
  bash workbench/spikes/hashtable/worker-packet-profile.sh "$SIXDB_RESULTS"
fi
