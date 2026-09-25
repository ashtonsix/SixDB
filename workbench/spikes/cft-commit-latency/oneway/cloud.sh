#!/bin/bash
set -euo pipefail
bash workbench/tools/worker-setup.sh
bash workbench/spikes/cft-commit-latency/oneway/phc-setup.sh
clang++-21 -std=c++23 -O2 -g -pthread workbench/spikes/cft-commit-latency/oneway/probe.cpp -o "$SIXDB_RESULTS/probe"
python3 workbench/spikes/cft-commit-latency/oneway/node.py
