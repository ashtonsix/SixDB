#!/bin/bash
# Prepare the next boot, then let this normal worker finish and collect.
# Capture the idle instance as an AMI with --no-reboot; do not reboot this job.
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
test "$(uname -m)" = aarch64
apt-get -o Acquire::Retries=3 update -qq
apt-get -o Acquire::Retries=3 install -y -qq linux-aws-64k
printf '%s\n' 'GRUB_FLAVOUR_ORDER=aws-64k' > /etc/default/grub.d/sixdb-64k.cfg
update-grub
python3 - <<'PY'
import glob, json, os, pathlib, platform, subprocess
result = {
    "format": 1,
    "current_page_bytes": os.sysconf("SC_PAGESIZE"),
    "current_kernel": platform.release(),
    "next_flavor": "aws-64k",
    "installed_kernels": sorted(glob.glob("/boot/vmlinuz-*")),
    "packages": subprocess.check_output(
        ["dpkg-query", "-W", "-f=${Package} ${Version}\\n", "linux-*aws*"], text=True),
    "note": "64KiB is selected for next boot; current job remains on its original kernel.",
}
assert any(name.endswith("-aws-64k") for name in result["installed_kernels"])
pathlib.Path(os.environ["SIXDB_RESULTS"], "prepared-kernel.json").write_text(
    json.dumps(result, indent=2) + "\n")
print(json.dumps(result, indent=2))
PY
