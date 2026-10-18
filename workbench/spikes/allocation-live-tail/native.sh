#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}" "${SIXDB_CPU:?}"
extra=()
if [[ -n ${SIXDB_RECLAIMER_CPU:-} ]]; then extra+=(--reclaimer-cpu "$SIXDB_RECLAIMER_CPU"); fi
python3 -B workbench/spikes/allocation-live-tail/run.py \
  --output "$SIXDB_RESULTS/comparison" --cpu "$SIXDB_CPU" \
  --tune "${SIXDB_LIVE_TAIL_TUNE:-generic}" --march "${SIXDB_MARCH:-native}" \
  --trials "${SIXDB_LIVE_TAIL_TRIALS:-6}" --loops "${SIXDB_LIVE_TAIL_LOOPS:-1048576}" \
  --batch "${SIXDB_LIVE_TAIL_BATCH:-128}" "${extra[@]}"
