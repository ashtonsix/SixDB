#!/bin/bash
set -euo pipefail
apt-get install -y -qq liburing-dev nvme-cli e2fsprogs hdparm ethtool
cmake --preset dev -DSIXDB_SPIKES=cft-commit-latency
cmake --build --preset dev --target cft_commit_bench -j "$SIXDB_BUILD_JOBS"
python3 workbench/spikes/cft-commit-latency/commit/node.py
