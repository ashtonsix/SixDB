#!/bin/bash
set -euo pipefail
apt-get install -y -qq iputils-ping ethtool
clang++-21 -std=c++23 -O2 -g -pthread workbench/spikes/cft-commit-latency/probe.cpp -o "$SIXDB_RESULTS/probe"
python3 workbench/spikes/cft-commit-latency/node.py
