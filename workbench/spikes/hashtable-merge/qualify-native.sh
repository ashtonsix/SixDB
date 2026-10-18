#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}" "${SIXDB_CPU:?}" "${MERGE_CPU:?}"
case "$MERGE_CPU" in
 zen5) march=znver5;; granite-rapids) march=graniterapids;;
 neoverse-v2) march=armv8-a+simd;; *) exit 2;;
esac
mode=${MERGE_MODE:-16}
[[ ${MERGE_COMPARE_REFACTOR:-0} == 0 ]] || { echo "Replay archived paired comparisons from their source capture" >&2; exit 2; }
case "$mode" in 16|18) ;;*) exit 2;;esac
kind=${MERGE_REDUCER_KIND:-0}
reps=${MERGE_REPETITIONS:-4}
(( reps > 0 && reps % 4 == 0 )) || { echo "Use complete four-owner rotations" >&2; exit 2; }
panel=${MERGE_PANEL:-basic}
values=(${MERGE_VALUES:-8})
targets=(hashtable_merge_check)
for v in "${values[@]}";do targets+=("hashtable_merge_bench_$v");done
wide=OFF;size=OFF
case "$panel" in basic|k30) ;;wide) wide=ON;;size) size=ON;;*) exit 2;;esac
clang++-21 --version > "$SIXDB_RESULTS/compiler.txt"
lscpu --json > "$SIXDB_RESULTS/lscpu.json"
build=build/hashtable-merge-qualify-$mode-$kind-$panel
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
 -DSIXDB_SPIKES=hashtable-merge -DSIXDB_BENCHMARKS= \
 -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$MERGE_CPU" -DCMAKE_CXX_FLAGS=-DNDEBUG \
 -DSIXDB_MERGE_ONLY_MODE="$mode" -DSIXDB_MERGE_REDUCER_KIND="$kind" \
 -DSIXDB_MERGE_WIDE_FAMILIES="$wide" -DSIXDB_MERGE_SIZE_SWEEP="$size" \
 > "$SIXDB_RESULTS/configure.log" 2>&1
cmake --build "$build" --target "${targets[@]}" -j "${SIXDB_BUILD_JOBS:-2}" > "$SIXDB_RESULTS/build.log" 2>&1
"$build/workbench/spikes/hashtable-merge/hashtable_merge_check" > "$SIXDB_RESULTS/check.txt"
cp "$build/compile_commands.json" "$SIXDB_RESULTS/compile.json"
for v in "${values[@]}";do
 cp "$build/workbench/spikes/hashtable-merge/hashtable_merge_bench_$v" "$SIXDB_RESULTS/bench-$v"
 llvm-objdump-21 -d --demangle "$build/workbench/spikes/hashtable-merge/hashtable_merge_bench_$v" | gzip > "$SIXDB_RESULTS/bench-$v.asm.gz"
done
python3 - "$SIXDB_RESULTS" "$build" "$panel" "$mode" "$kind" "$reps" "${values[@]}" <<'RUN'
import sys,json,subprocess,itertools
from pathlib import Path
root,build=map(Path,sys.argv[1:3]);panel=sys.argv[3];mode,kind,reps=map(int,sys.argv[4:7]);values=list(map(int,sys.argv[7:]));modes=[mode]
cases={
 'basic':[(k,b,m)for k,b in((14,10),(30,10),(30,15))for m in(2,8)],
 'k30':[(30,b,8)for b in(10,15)],
 'wide':[(k,b,m)for k,b in((31,10),(47,10),(47,17),(55,9))for m in(2,8)],
 'size':[(30,b,8)for b in range(10,16)],
}[panel]
overlaps=[0,50]if panel=='size'else[0,50,100]
(root/'cases.json').write_text(json.dumps(dict(modes=modes,values=values,cases=cases,overlaps=overlaps,reducer_kind=kind,repetitions=reps,order_scheme="balanced_latin_square",source_conditioning="incoming_entry_prewalk",owners=['accumulate','kway','scalar_reference','abseil'])))
import os
for v,seed,(k,b,m),o,mode in itertools.product(values,(713,3191,76543),cases,overlaps,modes):
 name=f'mode{mode}-v{v}-k{k}-b{b}-m{m}-o{o}-s{seed}.csv'
 with(root/name).open('w')as out:
  subprocess.run(['taskset','-c',os.environ['SIXDB_CPU'],str(build/f'workbench/spikes/hashtable-merge/hashtable_merge_bench_{v}'),*map(str,(k,b,m,o,seed,reps,mode))],stdout=out,check=True)
 with(root/'progress.txt').open('a')as out:out.write(name+' complete\n')
RUN
san=build/hashtable-merge-qualify-san
cmake -S . -B "$san" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=hashtable-merge -DSIXDB_BENCHMARKS= \
 -DSIXDB_MARCH="$march" -DSIXDB_TUNE="$MERGE_CPU" \
 -DCMAKE_CXX_FLAGS_RELEASE='-O1 -DNDEBUG' \
 -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fsanitize-address-use-after-return=always -fno-omit-frame-pointer' > "$SIXDB_RESULTS/san-configure.log" 2>&1
cmake --build "$san" --target hashtable_merge_check -j "${SIXDB_BUILD_JOBS:-2}" > "$SIXDB_RESULTS/san-build.log" 2>&1
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$san/workbench/spikes/hashtable-merge/hashtable_merge_check" > "$SIXDB_RESULTS/sanitize.txt"
printf '%s completed\n' "$MERGE_CPU" > "$SIXDB_RESULTS/complete.txt"
