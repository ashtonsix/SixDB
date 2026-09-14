#!/bin/bash
set -euo pipefail
apt-get install -y -qq liburing-dev nvme-cli e2fsprogs hdparm trace-cmd strace
cmake --preset dev -DSIXDB_SPIKES=cft-commit-latency
cmake --build --preset dev --target cft_persistence_bench -j "$SIXDB_BUILD_JOBS"
python3 workbench/spikes/cft-commit-latency/persistence/d3-diagnostic.py
