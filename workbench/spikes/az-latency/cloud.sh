#!/bin/bash
set -euo pipefail
apt-get install -y -qq iputils-ping ethtool
clang++-21 -std=c++23 -O2 -g -pthread workbench/spikes/az-latency/probe.cpp -o "$SIXDB_RESULTS/probe"
python3 workbench/spikes/az-latency/node.py
