#!/usr/bin/env bash
set -euo pipefail
: "${SIXDB_SOURCE:?}"
: "${SIXDB_RESULTS:?}"
cd "$SIXDB_SOURCE"
if ! pkg-config --exists lmdb; then sudo apt-get update -qq; sudo apt-get install -y -qq liblmdb-dev; fi
if [[ ! -x /usr/bin/time ]]; then sudo apt-get update -qq; sudo apt-get install -y -qq time; fi
python3 workbench/spikes/orbital-mvcc/performance/run.py --output "$SIXDB_RESULTS/breadth" \
 --profile breadth --repetitions 2 --cpu "$SIXDB_CPU" --jobs 1 --march x86-64-v3 --tune zen5
