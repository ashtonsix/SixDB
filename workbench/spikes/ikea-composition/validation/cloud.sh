#!/bin/bash
set -euo pipefail
exec python3 workbench/spikes/ikea-composition/validation/run.py "$@"
