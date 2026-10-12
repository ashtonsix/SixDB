#!/bin/bash
set -euo pipefail
python3 - <<'PY'
import json, os, pathlib, platform, subprocess
page = os.sysconf("SC_PAGESIZE")
assert page == 65536, f"expected native 64KiB pages, got {page}"
assert platform.release().endswith("-aws-64k"), platform.release()
receipt = {"format": 1, "native_page_bytes": page, "kernel": platform.release(),
           "machine": platform.machine(),
           "compiler": subprocess.check_output(["clang++-21", "--version"], text=True)}
pathlib.Path(os.environ["SIXDB_RESULTS"], "native-platform.json").write_text(
    json.dumps(receipt, indent=2) + "\n")
print(json.dumps(receipt, indent=2))
PY
