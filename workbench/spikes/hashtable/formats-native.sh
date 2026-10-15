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
  if [[ $isa == avx512 ]]; then
    if [[ $tune == zen5 ]]; then march=znver5; else march=graniterapids; fi
  fi
fi
access=${HASHTABLE_ACCESS:-0}
append=${HASHTABLE_APPEND:-0}
reduction=${HASHTABLE_LOCAL_REDUCTION:-0}
bags=${HASHTABLE_BAGS:-0}
inline=${HASHTABLE_INLINE:-0}
target=hashtable_design_formats
report_args=()
if [[ $access == 1 ]]; then target=hashtable_design_access; report_args=(--access); fi
if [[ $append == 1 ]]; then
  [[ $access == 0 ]]
  target=hashtable_design_append; report_args=(--append)
fi
if [[ $reduction == 1 ]]; then
  [[ $append == 0 && ( $access == 1 || $bags == 1 || $inline == 1 ) ]]
  if [[ $access == 1 ]]; then target=hashtable_design_reduction; fi
fi
if [[ $bags == 1 ]]; then
  [[ $access == 0 && $append == 0 && $reduction == 1 ]]
  target=hashtable_design_bag_formats; report_args=(--bags)
fi
if [[ $inline == 1 ]]; then
  [[ $access == 0 && $append == 0 && $bags == 0 && $reduction == 1 ]]
  target=hashtable_design_inline_formats; report_args=(--inline)
fi
build="build/hashtable-design-formats-$isa-$access-$append-$reduction-$bags-$inline"
checks=(hashtable_design_recipe_view_check)
if [[ $append == 1 || $reduction == 1 ]]; then checks+=(hashtable_design_links_reservation_check); fi
if [[ $bags == 1 ]]; then checks+=(hashtable_design_buffer_bags_check hashtable_design_buffer_publication); fi
if [[ $inline == 1 ]]; then checks+=(hashtable_design_inline_bag_check hashtable_design_inline_publication); fi
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_SPIKES=hashtable -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$tune" \
  > "$SIXDB_RESULTS/configure.log" 2>&1
/usr/bin/time -v -o "$SIXDB_RESULTS/build-resources.txt" \
  cmake --build "$build" --target "$target" "${checks[@]}" -j 1 \
  > "$SIXDB_RESULTS/build.log" 2>&1
exe="$build/workbench/spikes/hashtable/$target"
cp "$exe" "$SIXDB_RESULTS/formats"
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
llvm-objdump-21 -d --demangle --no-show-raw-insn "$exe" | gzip > "$SIXDB_RESULTS/assembly.gz"
llvm-nm-21 --print-size --size-sort --demangle "$exe" > "$SIXDB_RESULTS/symbols.txt"
taskset -c "$SIXDB_CPU" "$exe" check > "$SIXDB_RESULTS/check.txt"
taskset -c "$SIXDB_CPU" "$build/workbench/spikes/hashtable/hashtable_design_recipe_view_check" > "$SIXDB_RESULTS/recipe-check.txt"
if [[ $append == 1 || $reduction == 1 ]]; then
  taskset -c "$SIXDB_CPU" "$build/workbench/spikes/hashtable/hashtable_design_links_reservation_check" > "$SIXDB_RESULTS/links-check.txt"
fi
if [[ $bags == 1 ]]; then
  taskset -c "$SIXDB_CPU" "$build/workbench/spikes/hashtable/hashtable_design_buffer_bags_check" > "$SIXDB_RESULTS/buffer-check.txt"
  taskset -c "$SIXDB_CPU" "$build/workbench/spikes/hashtable/hashtable_design_buffer_publication" check > "$SIXDB_RESULTS/buffer-publication-check.txt"
fi
if [[ $inline == 1 ]]; then
  taskset -c "$SIXDB_CPU" "$build/workbench/spikes/hashtable/hashtable_design_inline_bag_check" > "$SIXDB_RESULTS/inline-check.txt"
  taskset -c "$SIXDB_CPU" "$build/workbench/spikes/hashtable/hashtable_design_inline_publication" check > "$SIXDB_RESULTS/inline-publication-check.txt"
fi
/usr/bin/time -v -o "$SIXDB_RESULTS/bench-resources.txt" python3 - "$exe" <<'PY'
import csv, os, random, subprocess, sys
from pathlib import Path
out=Path(os.environ['SIXDB_RESULTS'])
arms=8 if os.environ.get('HASHTABLE_INLINE','0')=='1' else 6 if os.environ.get('HASHTABLE_APPEND','0')=='1' or os.environ.get('HASHTABLE_BAGS','0')=='1' else 10 if os.environ.get('HASHTABLE_ACCESS','0')=='1' else 16
cases=[(k, kind, arm, large) for k in range(8,65,8) for kind in range(3)
       for arm in range(arms) for large in ((0,) if k==8 else (0,1))]
random.Random(127).shuffle(cases)
with (out/'formats.csv').open('w') as f, (out/'processes.csv').open('w') as log:
    writer=None
    log.write('order,K,kind,arm,large\n')
    for at,(k,kind,arm,large) in enumerate(cases):
        command=['taskset','-c',os.environ['SIXDB_CPU'],sys.argv[1],'run',str(k),str(kind),str(arm),os.environ.get('HASHTABLE_REPEATS','9'),str(large)]
        result=subprocess.run(command,text=True,stdout=subprocess.PIPE,check=True)
        rows=list(csv.DictReader(result.stdout.splitlines()))
        if writer is None:
            writer=csv.DictWriter(f,fieldnames=rows[0].keys());writer.writeheader()
        writer.writerows(rows);f.flush()
        log.write(f'{at},{k},{kind},{arm},{large}\n');log.flush()
PY
python3 -B workbench/spikes/hashtable/formats-report.py "$SIXDB_RESULTS/formats.csv" "${report_args[@]}" \
  --complete --output "$SIXDB_RESULTS/summary.json" > "$SIXDB_RESULTS/summary.txt"

if [[ $inline == 1 ]]; then
  /usr/bin/time -v -o "$SIXDB_RESULTS/fanout-resources.txt" \
    python3 -B workbench/spikes/hashtable/inline-fanout.py "$exe" "$SIXDB_RESULTS" --reps "${HASHTABLE_REPEATS:-9}" --cpu "$SIXDB_CPU"
fi
