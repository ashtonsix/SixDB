#!/bin/bash
set -euo pipefail
apt-get install -y -qq liburing-dev nvme-cli ethtool
cmake --preset dev -DSIXDB_SPIKES=cft-commit-latency
cmake --build --preset dev --target cft_throughput_bench -j "$SIXDB_BUILD_JOBS"
python3 workbench/spikes/cft-commit-latency/commit/throughput_node.py
