#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?Use worker.py or choose results directory}"
: "${SIXDB_CPU:?Choose an allowed CPU}"
python3 -B - "$SIXDB_RESULTS" "$SIXDB_CPU" <<'PY'
import json
from pathlib import Path
import sys
from workbench.benchmarks.orbital.pmu_probe import catalog

root, cpu = Path(sys.argv[1]), int(sys.argv[2])
root.mkdir(parents=True, exist_ok=True)
(root / 'memory-pmu-catalog.json').write_text(json.dumps(catalog(cpu), indent=2) + '\n')
PY
events=(--pmu-event l1d-load-misses:3:0x10000)
if [[ "${MEMORY_ARM_CACHE_EVENTS:-0}" == 1 ]]; then
  python3 -B - "$SIXDB_RESULTS" <<'PY'
import json
from pathlib import Path
import sys

root = Path(sys.argv[1])
devices = json.loads((root / 'memory-pmu-catalog.json').read_text())['event_sources']
arm = [(name, int(device['type'])) for name, device in devices.items()
       if name.startswith(('armv8_pmuv3', 'armv9_pmuv3'))]
if len(arm) != 1:
    raise SystemExit('Choose a guest exposing one ARM core PMU for this diagnostic')
name, device_type = arm[0]
# PERF_TYPE_RAW and the guest's dynamic device type are separate controls.
# Advertised aliases do not establish that the hypervisor permits an event.
types = [('raw', 4)]
if device_type != 4:
    types.append((name, device_type))
events = [f'{prefix}-{level}:{kind}:{config}' for prefix, kind in types
          for level, config in [('l1d-refill', '0x3'), ('l2d-refill', '0x17'),
                                ('l3d-refill', '0x2a')]]
(root / 'memory-pmu-arm-events.txt').write_text('\n'.join(events) + '\n')
PY
  while IFS= read -r event; do
    events+=(--pmu-event "$event")
  done < "$SIXDB_RESULTS/memory-pmu-arm-events.txt"
fi
python3 -B workbench/spikes/memory-characterisation/run.py \
  --profile screen --pmu basic "${events[@]}" \
  --cpu "$SIXDB_CPU" --output "$SIXDB_RESULTS/memory-pmu" "$@"
