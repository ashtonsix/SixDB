#!/bin/bash
# Build and run the hot-path benchmark on a worker. Arguments after -- pass to
# the driver. HOT_TRIALS fresh processes alternate any candidate builds listed
# in HOT_VARIANTS (production is always "baseline").
set -euo pipefail
: "${SIXDB_RESULTS:?}" "${SIXDB_CPU:?}"
march=${SIXDB_MARCH:-}
tune=${SIXDB_TUNE:-generic}
config=(-DSIXDB_SPIKES=orbital-hot-paths -DSIXDB_TUNE="$tune")
[[ -n $march ]] && config+=(-DSIXDB_MARCH="$march")
cmake --preset release -B build/hot "${config[@]}" > "$SIXDB_RESULTS/configure.log"
targets=(orbital_hot_paths)
for variant in ${HOT_VARIANTS:-}; do targets+=("orbital_hot_paths_$variant"); done
cmake --build build/hot --target "${targets[@]}" -j "${SIXDB_BUILD_JOBS:-4}" > "$SIXDB_RESULTS/build.log"
allowed=($(python3 -c 'import os; print(*sorted(os.sched_getaffinity(0)))'))
cpu=${allowed[0]}
peers=$(IFS=,; echo "${allowed[*]:1:3}")
lscpu > "$SIXDB_RESULTS/lscpu.txt" || true
for trial in $(seq 1 "${HOT_TRIALS:-3}"); do
  for target in "${targets[@]}"; do
    name=${target#orbital_hot_paths}; name=${name#_}; name=${name:-baseline}
    build/hot/workbench/spikes/orbital-hot-paths/$target --cpu "$cpu" --peers "$peers" "$@" \
      > "$SIXDB_RESULTS/$name-$trial.csv"
  done
done
