#!/bin/bash
# Same rich-format source and complete operations; separate ISA from CPU tuning.
set -euo pipefail
: "${SIXDB_RESULTS:?}" "${SIXDB_CPU:?}" "${HASHTABLE_TUNE:?}"
root=$SIXDB_RESULTS
fp_bits=${HASHTABLE_FP_BITS:-7}
[[ $fp_bits == 7 || $fp_bits == 8 ]] || exit 2
export HASHTABLE_FP_BITS=$fp_bits
study=${HASHTABLE_RICH_STUDY:-features}
export HASHTABLE_RICH_STUDY=$study
case "$HASHTABLE_TUNE" in
  zen5) tune=znver5; native=znver5; tiers=(avx2 avx2-gfni native) ;;
  granite-rapids) tune=graniterapids; native=graniterapids; tiers=(avx2 avx2-gfni native) ;;
  neoverse-v2) tune=neoverse-v2; native=armv8-a+simd; tiers=(neon) ;;
  *) echo 'This bounded study requires Zen5, Granite, or V2.' >&2; exit 2 ;;
esac
if [[ $study == mask ]]; then
  [[ $HASHTABLE_TUNE == granite-rapids && $fp_bits == 8 ]] || exit 2
  tiers=(c64r32 c32r64 c32r32 c64r64)
else
  [[ $study == features ]] || exit 2
fi
lscpu --json > "$root/lscpu.json"
clang++-21 --version > "$root/compiler.txt"
python3 -B - "$root" "$SIXDB_CPU" <<'PY'
import hashlib,json,runpy,sys
from pathlib import Path
root=Path(sys.argv[1])
discover=runpy.run_path('workbench/spikes/memory-characterisation/hardware.py')['discover']
(root/'hardware.json').write_text(json.dumps(discover(int(sys.argv[2])),indent=2)+'\n')
names=['rich_composition.cpp','surface-report.py','rich-features-native.sh']
(root/'study-sources.json').write_text(json.dumps({n:hashlib.sha256(Path('workbench/spikes/hashtable',n).read_bytes()).hexdigest() for n in names},indent=2)+'\n')
PY
progress() {
  python3 -B - "$1" "$root" <<'PY'
import json,os,sys,time
from pathlib import Path
Path(sys.argv[2],'progress.json').write_text(json.dumps(dict(phase=sys.argv[1],updated_at=time.time()))+'\n')
if os.environ.get('SIXDB_GROUP_URI'):
    sys.path.insert(0,'workbench/tools')
    from worker_context import GroupContext
    GroupContext.from_env().progress(sys.argv[1])
PY
}
printf '%s\n' "${tiers[@]}" > "$root/tiers.txt"
for tier in "${tiers[@]}"; do
  progress "build-$tier"
  dest="$root/$tier"; build="build/hashtable-rich-features-$tier"
  mkdir -p "$dest/codegen"
  march=x86-64-v3; extra=()
  composed_bits=64; raw_bits=32
  if [[ $study == mask ]]; then
    composed_bits=${tier:1:2}; raw_bits=${tier:4:2}
  fi
  [[ $tier != avx2-gfni ]] || extra=(-mgfni)
  [[ $tier != native && $tier != neon ]] || march=$native
  # Macro capture uses the same feature/tune flags as the target, and catches
  # accidental feature changes from driver option ordering.
  clang++-21 -DHT_RICH_FP_BITS="$fp_bits" -DHT_RICH_COMPOSED_MASK_BITS="$composed_bits" \
    -DHT_RICH_RAW_MASK_BITS="$raw_bits" "${extra[@]}" -march="$march" -mtune="$tune" -dM -E -x c++ /dev/null > "$dest/macros.txt"
  cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSIXDB_SPIKES=hashtable -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$HASHTABLE_TUNE" \
    -DCMAKE_CXX_FLAGS="-DHT_RICH_FP_BITS=$fp_bits -DHT_RICH_COMPOSED_MASK_BITS=$composed_bits -DHT_RICH_RAW_MASK_BITS=$raw_bits ${extra[*]}" > "$dest/configure.log" 2>&1
  /usr/bin/time -v -o "$dest/build-resources.txt" cmake --build "$build" \
    --target hashtable_design_rich_composition -j 1 > "$dest/build.log" 2>&1
  cp "$build/workbench/spikes/hashtable/hashtable_design_rich_composition" "$dest/rich_composition"
  cp "$build/compile_commands.json" "$dest/compile.json"
  llvm-objdump-21 -d --demangle --no-show-raw-insn "$dest/rich_composition" | gzip > "$dest/codegen/rich_composition.s.gz"
  llvm-nm-21 --print-size --size-sort --demangle "$dest/rich_composition" > "$dest/codegen/rich_composition.symbols.txt"
  progress "check-$tier"
  /usr/bin/time -v -o "$dest/check-resources.txt" taskset -c "$SIXDB_CPU" "$dest/rich_composition" check > "$dest/check.txt" 2>&1
  python3 -B workbench/spikes/hashtable/surface-report.py "$dest" --codegen-only > "$dest/codegen-summary.txt"
done
python3 -B - "$root" <<'PY'
import json,os,sys
from pathlib import Path
root=Path(sys.argv[1]);features={}
for tier in (root/'tiers.txt').read_text().split():
    text=(root/tier/'macros.txt').read_text()
    features[tier]={name:('#define '+name+' ') in text for name in ['__AVX2__','__GFNI__','__AVX512F__','__AVX512BW__','__AVX512VL__','__AVX512VBMI__']}
    features[tier]['fingerprint_bits']=int(os.environ['HASHTABLE_FP_BITS'])
    assert '#define HT_RICH_FP_BITS '+os.environ['HASHTABLE_FP_BITS'] in text
    if os.environ['HASHTABLE_RICH_STUDY']=='mask':
        features[tier]['composed_mask_bits']=int(tier[1:3])
        features[tier]['raw_mask_bits']=int(tier[4:6])
        assert '#define HT_RICH_COMPOSED_MASK_BITS '+tier[1:3] in text
        assert '#define HT_RICH_RAW_MASK_BITS '+tier[4:6] in text
        assert features[tier]['__AVX2__'] and not features[tier]['__GFNI__'] and not features[tier]['__AVX512F__']
    elif tier=='neon':
        assert '#define __aarch64__ 1' in text and '#define __ARM_NEON 1' in text
        assert not any(features[tier][k] for k in ['__AVX2__','__GFNI__','__AVX512F__'])
    else:
        assert features[tier]['__AVX2__']
        assert features[tier]['__GFNI__']==(tier!='avx2')
        assert features[tier]['__AVX512F__']==(tier=='native')
(root/'features.json').write_text(json.dumps(features,indent=2)+'\n')
PY
# Three complete process-order rotations; each process rotates its four arms
# internally over eight repetitions. Guards/warmups are part of the same driver.
for pass in 0 1 2; do
  for ((step=0;step<${#tiers[@]};++step)); do
    tier=${tiers[$(((pass+step)%${#tiers[@]}))]}
    for buckets in 1 256; do
      progress "measure-pass$pass-$tier-$buckets"
      /usr/bin/time -v -o "$root/$tier/pass$pass-rich-$buckets-resources.txt" \
        taskset -c "$SIXDB_CPU" "$root/$tier/rich_composition" bench "$buckets" 32 8 \
        > "$root/$tier/pass$pass-rich-$buckets.csv"
    done
  done
done
progress summarize
python3 -B - "$root" <<'PY' > "$root/summary.txt"
import json,os,runpy,statistics,sys
from pathlib import Path
root=Path(sys.argv[1]);read=runpy.run_path('workbench/spikes/hashtable/surface-report.py')['rich']
cases=[]
for tier in (root/'tiers.txt').read_text().split():
    for process in range(3):
        for buckets in [1,256]:
            cases.extend(dict(x,tier=tier,process=process) for x in read(root/tier/f'pass{process}-rich-{buckets}.csv'))
fp=int(os.environ['HASHTABLE_FP_BITS'])
scope=[('F7+C13 is an independent noncanonical primitive stress case.' if fp==7 else 'F8+C13 matches K40/A19 bucket components, but excludes hash/table costs.'),
       'Same frozen rich-format driver/header source for all tiers and hosts.',
       ('Granite AVX2 only: composed/raw candidate-mask widths cross over independently; bytes, SIMD predicates and logical queries remain fixed.' if os.environ['HASHTABLE_RICH_STUDY']=='mask' else
        'AVX2 and AVX2+GFNI are feature-matched across x86 hosts; native enables additional platform-specific features. V2 has a single NEON tier.'),
       'No cache-residence claim; allocated payload, query state and shared controls all occupy memory.',
       'Three rotated process passes; each cell below is the median of eight internal repetitions per process.',
       'All five complete operations/tails/layouts retained; raw fused independently encodes and routes the same bytes.',
       'This is a primitive composition control, not a complete hash table or a multi-writer result.']
(root/'summary.json').write_text(json.dumps(dict(scope=scope,cases=cases),indent=2)+'\n')
for row in [6,10]:
    for op in ['independent_hit','serial_hit','changed_update']:
        for tier in (root/'tiers.txt').read_text().split():
            result=[]
            for arm in ['sp_tp_split','raw_fused']:
                times=[x['ns_per_op'] for x in cases if x['row_bytes']==row and x['operation']==op and x['tier']==tier and x['arm']==arm and x['buckets']==256 and x['active']==16]
                assert len(times)==3
                result.append(f'{arm} median={statistics.median(times):.4f} range=[{min(times):.4f},{max(times):.4f}]')
            print(f'F{fp} row={row} active=16 buckets=256 {op} {tier}: '+ '; '.join(result))
for line in scope:print(line)
PY
progress complete
