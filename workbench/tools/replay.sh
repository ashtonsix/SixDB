#!/bin/bash
set -euo pipefail
exec python3 workbench/tools/replay.py "$@"
