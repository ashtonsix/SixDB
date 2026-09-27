#!/bin/bash
set -euo pipefail
bash workbench/tools/tlc/java.sh
exec python3 workbench/tools/tlc/tlc.py run "$@"
