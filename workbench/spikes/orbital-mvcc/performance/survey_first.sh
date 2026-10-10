#!/usr/bin/env bash
set -euo pipefail
: "${SIXDB_SOURCE:?worker source missing}"
: "${SIXDB_RESULTS:?worker results missing}"
cd "$SIXDB_SOURCE"
if ! pkg-config --exists lmdb; then
  sudo apt-get update -qq
  sudo apt-get install -y -qq liblmdb-dev
fi
if [[ ! -x /usr/bin/time ]]; then
  sudo apt-get update -qq
  sudo apt-get install -y -qq time
fi
python3 workbench/spikes/orbital-mvcc/performance/run.py \
  --output "$SIXDB_RESULTS/survey" --profile survey-first \
  --cpu "${SIXDB_CPU:?worker CPU missing}" --jobs "${SIXDB_BUILD_JOBS:-1}" \
  --repetitions "${SIXDB_REPETITIONS:-2}" --march x86-64-v3 --tune zen5
