#!/bin/bash
# Only the explicitly requested fresh, disposable fleet worker may prepare this device.
set -euo pipefail
: "${SIXDB_WORKER_ID:?fleet worker required}"
: "${SIXDB_RESULTS:?fleet results required}"
[[ "${SIXDB_WORKER_REUSED:-1}" == 0 ]]
[[ "${SIXDB_PREPARE_INSTANCE_STORE:-0}" == 1 ]]
store_device=$(python3 - <<'PY'
from pathlib import Path
import json
import subprocess
for root in sorted(Path('/sys/block').glob('nvme*n1')):
    try:
        model = (root / 'device/model').read_text().strip()
    except OSError:
        continue
    if model != 'Amazon EC2 NVMe Instance Storage':
        continue
    device = '/dev/' + root.name
    result = json.loads(subprocess.check_output(['lsblk', '--json', '-o', 'NAME,FSTYPE,MOUNTPOINTS', device]))['blockdevices'][0]
    if result.get('fstype') or result.get('children') or any(result.get('mountpoints') or []):
        raise SystemExit('Refusing an instance-store device with a filesystem, partitions or mounts')
    signatures = subprocess.check_output(['sudo', 'wipefs', '--no-act', '--noheadings', device], text=True)
    if signatures.strip():
        raise SystemExit('Refusing a device with existing signatures')
    print(device)
    break
else:
    raise SystemExit('No empty EC2 instance-store NVMe device found')
PY
)
# Use a 1 GiB filesystem for the 64 MiB diagnostic; never touch an EBS device.
sudo mkfs.ext4 -F -b 4096 -E nodiscard,lazy_itable_init=0,lazy_journal_init=0 "$store_device" 262144 > "$SIXDB_RESULTS/instance-store-setup.txt" 2>&1
sudo mkdir -p /mnt/sixdb-characterisation-store
sudo mount "$store_device" /mnt/sixdb-characterisation-store
sudo chmod 1777 /mnt/sixdb-characterisation-store
