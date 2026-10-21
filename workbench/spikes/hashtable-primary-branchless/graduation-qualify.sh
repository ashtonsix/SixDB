#!/bin/bash
set -euo pipefail
study=workbench/spikes/hashtable-primary-branchless
root="$SIXDB_SOURCE/build/hashtable-primary-graduation-native"
build="$root/sanitize"
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
 -DCMAKE_CXX_FLAGS_DEBUG='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -DNDEBUG' \
 -DCMAKE_CXX_FLAGS=-DHINT_MODE=2 -DSIXDB_MARCH=znver5 -DSIXDB_TUNE=zen5 \
 -DSIXDB_SPIKES=hashtable-boost-adoption -DMEMORY_CASES=32_11 \
 -DMEMORY_WIDTHS=32 '-DMEMORY_VALUES=0;8' -DMEMORY_BENCH_SOURCE="$root/hint-bench.cpp" \
 > "$SIXDB_RESULTS/sanitize-configure.log" 2>&1
cmake --build "$build" --target ikea_hashtable_lookup_check ikea_hashtable_selector_check \
 ikea_example_hashtable_adaptive memory_ikea_32_11_0 memory_ikea_32_11_8 \
 -j "${SIXDB_BUILD_JOBS:-2}" > "$SIXDB_RESULTS/sanitize-build.log" 2>&1
ctest --test-dir "$build" --output-on-failure -R '^ikea_(hashtable_(lookup|selector)_check|example_hashtable_adaptive)$' \
 > "$SIXDB_RESULTS/sanitize-checks.txt" 2>&1
python3 - "$build/workbench/spikes/hashtable-boost-adoption" "$SIXDB_RESULTS" <<'PY'
import csv,subprocess,sys
from pathlib import Path
build,out=map(Path,sys.argv[1:]);runs=0
for v in [0,8]:
 for seed in [713,991,1729]:
  for n in [1,31,32,33,255,256,257,511,512,513,4097]:
   for order in range(7):
    name=f'v{v}-s{seed}-n{n}-o{order}'
    cmd=[str(build/f'memory_ikea_32_11_{v}'),'--impl','ikea','--n','13926','--seed',str(seed),'--requests',str(n),'--repetitions','1','--writes','0','--query-order',str(order)]
    with (out/(name+'.csv')).open('w') as f,(out/(name+'.stderr')).open('w') as e:subprocess.run(cmd,stdout=f,stderr=e,check=True)
    rows=[r for r in csv.DictReader((out/(name+'.csv')).open()) if r['phase']=='read'];assert len(rows)==6
    assert all(int(r['method_predictable'])+int(r['method_unpredictable'])==n for r in rows)
    runs+=1
(out/'tails-check.txt').write_text(f'{runs} maintained-selector ASan/UBSan processes; both prefetch paths, all seven orders, 1..4097 request tails passed\n')
PY
