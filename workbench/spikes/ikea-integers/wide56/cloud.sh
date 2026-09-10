#!/bin/bash
set -euo pipefail
target="$1"
shift
python3 workbench/spikes/ikea-integers/wide56/run.py --target "$target" -- "$@"
