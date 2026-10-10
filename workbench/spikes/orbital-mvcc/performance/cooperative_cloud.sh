#!/bin/bash
set -euo pipefail
if ! pkg-config --exists lmdb; then
  sudo apt-get update -qq
  sudo apt-get install -y -qq liblmdb-dev
fi
python3 workbench/spikes/orbital-mvcc/performance/cooperative_run.py \
  --output "$SIXDB_RESULTS/cooperative" --cpu "$SIXDB_CPU" --jobs "$SIXDB_BUILD_JOBS"
