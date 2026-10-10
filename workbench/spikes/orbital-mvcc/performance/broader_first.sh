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
python3 workbench/spikes/orbital-mvcc/performance/broader_run.py \
  --output "$SIXDB_RESULTS/broad" --cpu "${SIXDB_CPU:?worker CPU missing}" \
  --jobs "${SIXDB_BUILD_JOBS:-1}" "$@"
