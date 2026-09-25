#!/bin/bash
set -euo pipefail
apt-get install -y -qq ethtool
if [[ ${ENABLE_PHC:-0} == 1 ]]; then
  bash workbench/spikes/cft-commit-latency/oneway/phc-setup.sh
fi
device=$(ip -j route show default | python3 -c 'import json,sys; print(json.load(sys.stdin)[0]["dev"])')
{
  uname -a
  ethtool -i "$device"
  ethtool -T "$device"
  modinfo ena
  ls -l /dev/ptp* || true
  cat /sys/module/ena/parameters/phc_enable || true
} > "$SIXDB_RESULTS/clock-hardware.txt" 2>&1
python3 workbench/spikes/cft-commit-latency/oneway/calibrate.py > "$SIXDB_RESULTS/calibration.jsonl"
