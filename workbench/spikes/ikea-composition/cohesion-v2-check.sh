#!/bin/bash
# Complete native interface/caller qualification; worker.py supplies resources.
set -euo pipefail
case "$(uname -m)" in
  aarch64) profiles=neon; tune=neoverse-v2 ;;
  *) profiles='avx2 avx512'; tune=zen5 ;;
esac
failed=0
for profile in $profiles; do
  case "$profile" in
    neon) march=armv8-a+simd ;;
    avx2) march=x86-64-v3 ;;
    avx512) march=znver5 ;;
  esac
  dir="build/ikea/interface-v2/$profile"
  cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" -DSIXDB_SPIKES="orbital-mvcc;hashtable" \
    -DSIXDB_TEST_SCOPE=full -DSIXDB_HEAVY_COMPILE_JOBS=4 \
    > "$SIXDB_RESULTS/$profile-configure.log" 2>&1
  # Build the selected executable targets, then run each test once. Metadata
  # labels identify the retained owner checks without pulling unrelated tests.
  mapfile -t targets < <(python3 - "$dir" "$SIXDB_RESULTS/$profile-selected" <<'PYTARGETS'
import json,pathlib,re,subprocess,sys
all_tests=json.loads(subprocess.check_output(['ctest','--test-dir',sys.argv[1],'--show-only=json-v1'],text=True))
selected=[]
for test in all_tests['tests']:
    labels=next((p['value'] for p in test.get('properties',[]) if p['name']=='LABELS'),[])
    name=test['name']
    if (name.startswith(('ikea_','hashtable_mm_')) or 'orbital-mvcc-metadata' in labels or
        name in ('engine_directory_check','loom_hint_check','loom_example_allocation','orbital_preparation_check')):
        command=test.get('command')
        target=(pathlib.Path(command[0]).name if pathlib.Path(command[0]).is_relative_to(pathlib.Path(sys.argv[1]).resolve()) else None) if command else name
        assert target is None or re.fullmatch('[A-Za-z0-9_]+',target),target
        selected.append({'name':name,'target':target})
assert selected
pathlib.Path(sys.argv[2]+'.json').write_text(json.dumps(selected,indent=2)+'\n')
pathlib.Path(sys.argv[2]+'.regex').write_text('^('+'|'.join(re.escape(t['name']) for t in selected)+')$')
print('\n'.join(sorted(set(t['target'] for t in selected if t['target']))))
PYTARGETS
  )
  test "${#targets[@]}" -gt 0
  cmake --build "$dir" --target "${targets[@]}" \
    -j "$SIXDB_BUILD_JOBS" -- -k 0 > "$SIXDB_RESULTS/$profile-build.log" 2>&1 || failed=1
  taskset -c "$SIXDB_CPU" ctest --test-dir "$dir" --output-on-failure --no-tests=error \
    -R "$(cat "$SIXDB_RESULTS/$profile-selected.regex")" \
    > "$SIXDB_RESULTS/$profile-check.log" 2>&1 || failed=1
  python3 - "$march" "$SIXDB_RESULTS/$profile-headers.json" <<'PY' || failed=1
import concurrent.futures,hashlib,json,os,pathlib,subprocess,sys
headers=sorted(p for p in pathlib.Path('ikea/include').rglob('*.h') if 'detail' not in p.parts)
def check_header(p):
    source='#include <'+str(p.relative_to('ikea/include'))+'>\n'
    command=['clang++-21','-std=c++23','-march='+sys.argv[1],'-Iikea/include','-x','c++','-fsyntax-only','-']
    result=subprocess.run(command,input=source,text=True,capture_output=True)
    return {'path':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),
            'returncode':result.returncode,'diagnostics':result.stderr}
with concurrent.futures.ThreadPoolExecutor(max_workers=min(4,int(os.environ.get('SIXDB_BUILD_JOBS','1')))) as pool:
    rows=list(pool.map(check_header,headers))
pathlib.Path(sys.argv[2]).write_text(json.dumps({'headers':rows},indent=2)+'\n')
failed=[row for row in rows if row['returncode']]
print(f'{len(headers)} independent public headers; {len(failed)} failed',flush=True)
raise SystemExit(bool(failed))
PY
  ctest --test-dir "$dir" --show-only=json-v1 > "$SIXDB_RESULTS/$profile-tests.json"
done

if test "${SIXDB_QUALIFY_CALLERS:-0}" = 1; then
  bash workbench/spikes/ikea-composition/cohesion-v2-callers.sh || failed=1
fi
exit "$failed"
