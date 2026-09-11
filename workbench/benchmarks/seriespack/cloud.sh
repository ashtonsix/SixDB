#!/bin/bash
set -euo pipefail
exec python3 workbench/benchmarks/seriespack/run.py "$@"
