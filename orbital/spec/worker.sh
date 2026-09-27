#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?worker results directory is required}"
mode=suite
if [[ ${1:-} == check || ${1:-} == recover ]]; then
  mode=$1
  shift
fi
bash workbench/tools/tlc/java.sh
output="$SIXDB_RESULTS/$mode"
if [[ $mode == recover ]]; then
  output=$SIXDB_RESULTS
fi
exec python3 "orbital/spec/$mode.py" "$@" --output "$output"
