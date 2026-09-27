#!/bin/bash
set -euo pipefail
exec python3 workbench/tools/tlc/tlc.py checkpoint "$1"
